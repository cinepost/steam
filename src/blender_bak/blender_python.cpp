/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Python.h>

#include "blender/CCL_api.h"

#include "blender/blender_device.h"
#include "blender/blender_session.h"
#include "blender/blender_sync.h"
#include "blender/blender_util.h"

#include "util/util_debug.h"
#include "util/util_foreach.h"
#include "util/util_logging.h"
#include "util/util_md5.h"
#include "util/util_opengl.h"
#include "util/util_path.h"
#include "util/util_string.h"
#include "util/util_types.h"

CCL_NAMESPACE_BEGIN

namespace {

/* Flag describing whether debug flags were synchronized from scene. */
bool debug_flags_set = false;

void *pylong_as_voidptr_typesafe(PyObject *object) {
  if (object == Py_None)
    return NULL;
  return PyLong_AsVoidPtr(object);
}

/* Synchronize debug flags from a given Blender scene.
 * Return truth when device list needs invalidation.
 */
bool debug_flags_sync_from_scene(BL::Scene b_scene) {
  DebugFlagsRef flags = DebugFlags();
  PointerRNA cscene = RNA_pointer_get(&b_scene.ptr, "steam");
  /* Backup some settings for comparison. */
  DebugFlags::OpenCL::DeviceType opencl_device_type = flags.opencl.device_type;
  /* Synchronize shared flags. */
  flags.viewport_static_bvh = get_enum(cscene, "debug_bvh_type");
  /* Synchronize CPU flags. */
  flags.cpu.avx2 = get_boolean(cscene, "debug_use_cpu_avx2");
  flags.cpu.avx = get_boolean(cscene, "debug_use_cpu_avx");
  flags.cpu.sse41 = get_boolean(cscene, "debug_use_cpu_sse41");
  flags.cpu.sse3 = get_boolean(cscene, "debug_use_cpu_sse3");
  flags.cpu.sse2 = get_boolean(cscene, "debug_use_cpu_sse2");
  flags.cpu.bvh_layout = (BVHLayout)get_enum(cscene, "debug_bvh_layout");
  flags.cpu.split_kernel = get_boolean(cscene, "debug_use_cpu_split_kernel");
  /* Synchronize CUDA flags. */
  flags.cuda.adaptive_compile = get_boolean(cscene, "debug_use_cuda_adaptive_compile");
  flags.cuda.split_kernel = get_boolean(cscene, "debug_use_cuda_split_kernel");
  /* Synchronize OptiX flags. */
  flags.optix.cuda_streams = get_int(cscene, "debug_optix_cuda_streams");
  /* Synchronize OpenCL device type. */
  switch (get_enum(cscene, "debug_opencl_device_type")) {
    case 0:
      flags.opencl.device_type = DebugFlags::OpenCL::DEVICE_NONE;
      break;
    case 1:
      flags.opencl.device_type = DebugFlags::OpenCL::DEVICE_ALL;
      break;
    case 2:
      flags.opencl.device_type = DebugFlags::OpenCL::DEVICE_DEFAULT;
      break;
    case 3:
      flags.opencl.device_type = DebugFlags::OpenCL::DEVICE_CPU;
      break;
    case 4:
      flags.opencl.device_type = DebugFlags::OpenCL::DEVICE_GPU;
      break;
    case 5:
      flags.opencl.device_type = DebugFlags::OpenCL::DEVICE_ACCELERATOR;
      break;
  }
  /* Synchronize other OpenCL flags. */
  flags.opencl.debug = get_boolean(cscene, "debug_use_opencl_debug");
  flags.opencl.mem_limit = ((size_t)get_int(cscene, "debug_opencl_mem_limit")) * 1024 * 1024;
  return flags.opencl.device_type != opencl_device_type;
}

/* Reset debug flags to default values.
 * Return truth when device list needs invalidation.
 */
bool debug_flags_reset() {
  DebugFlagsRef flags = DebugFlags();
  /* Backup some settings for comparison. */
  DebugFlags::OpenCL::DeviceType opencl_device_type = flags.opencl.device_type;
  flags.reset();
  return flags.opencl.device_type != opencl_device_type;
}

} /* namespace */

void python_thread_state_save(void **python_thread_state) {
  *python_thread_state = (void *)PyEval_SaveThread();
}

void python_thread_state_restore(void **python_thread_state) {
  PyEval_RestoreThread((PyThreadState *)*python_thread_state);
  *python_thread_state = NULL;
}

static const char *PyC_UnicodeAsByte(PyObject *py_str, PyObject **coerce) {
  const char *result = _PyUnicode_AsString(py_str);
  if (result) {
    /* 99% of the time this is enough but we better support non unicode
     * chars since blender doesn't limit this.
     */
    return result;
  } else {
    PyErr_Clear();
    if (PyBytes_Check(py_str)) {
      return PyBytes_AS_STRING(py_str);
    }
    else if ((*coerce = PyUnicode_EncodeFSDefault(py_str))) {
      return PyBytes_AS_STRING(*coerce);
    }
    else {
      /* Clear the error, so Cycles can be at least used without
       * GPU and OSL support,
       */
      PyErr_Clear();
      return "";
    }
  }
}

static PyObject *init_func(PyObject * /*self*/, PyObject *args) {
  PyObject *path, *user_path;
  int headless;

  if (!PyArg_ParseTuple(args, "OOi", &path, &user_path, &headless)) {
    return NULL;
  }

  PyObject *path_coerce = NULL, *user_path_coerce = NULL;
  path_init(PyC_UnicodeAsByte(path, &path_coerce),
            PyC_UnicodeAsByte(user_path, &user_path_coerce));
  Py_XDECREF(path_coerce);
  Py_XDECREF(user_path_coerce);

  BlenderSession::headless = headless;

  DebugFlags().running_inside_blender = true;

  VLOG(2) << "Debug flags initialized to:\n" << DebugFlags();

  Py_RETURN_NONE;
}

static PyObject *exit_func(PyObject * /*self*/, PyObject * /*args*/) {
  ShaderManager::free_memory();
  TaskScheduler::free_memory();
  Device::free_memory();
  Py_RETURN_NONE;
}

static PyObject *create_func(PyObject * /*self*/, PyObject *args) {
  PyObject *pyengine, *pypreferences, *pydata, *pyscreen, *pyregion, *pyv3d, *pyrv3d;
  int preview_osl;

  if (!PyArg_ParseTuple(args,
                        "OOOOOOOi",
                        &pyengine,
                        &pypreferences,
                        &pydata,
                        &pyscreen,
                        &pyregion,
                        &pyv3d,
                        &pyrv3d,
                        &preview_osl)) {
    return NULL;
  }

  /* RNA */
  ID *bScreen = (ID *)PyLong_AsVoidPtr(pyscreen);

  PointerRNA engineptr;
  RNA_pointer_create(NULL, &RNA_RenderEngine, (void *)PyLong_AsVoidPtr(pyengine), &engineptr);
  BL::RenderEngine engine(engineptr);

  PointerRNA preferencesptr;
  RNA_pointer_create(
      NULL, &RNA_Preferences, (void *)PyLong_AsVoidPtr(pypreferences), &preferencesptr);
  BL::Preferences preferences(preferencesptr);

  PointerRNA dataptr;
  RNA_main_pointer_create((Main *)PyLong_AsVoidPtr(pydata), &dataptr);
  BL::BlendData data(dataptr);

  PointerRNA regionptr;
  RNA_pointer_create(bScreen, &RNA_Region, pylong_as_voidptr_typesafe(pyregion), &regionptr);
  BL::Region region(regionptr);

  PointerRNA v3dptr;
  RNA_pointer_create(bScreen, &RNA_SpaceView3D, pylong_as_voidptr_typesafe(pyv3d), &v3dptr);
  BL::SpaceView3D v3d(v3dptr);

  PointerRNA rv3dptr;
  RNA_pointer_create(bScreen, &RNA_RegionView3D, pylong_as_voidptr_typesafe(pyrv3d), &rv3dptr);
  BL::RegionView3D rv3d(rv3dptr);

  /* create session */
  BlenderSession *session;

  if (rv3d) {
    /* interactive viewport session */
    int width = region.width();
    int height = region.height();

    session = new BlenderSession(engine, preferences, data, v3d, rv3d, width, height);
  }
  else {
    /* offline session or preview render */
    session = new BlenderSession(engine, preferences, data, preview_osl);
  }

  return PyLong_FromVoidPtr(session);
}

static PyObject *free_func(PyObject * /*self*/, PyObject *value) {
  delete (BlenderSession *)PyLong_AsVoidPtr(value);

  Py_RETURN_NONE;
}

static PyObject *render_func(PyObject * /*self*/, PyObject *args) {
  PyObject *pysession, *pydepsgraph;

  if (!PyArg_ParseTuple(args, "OO", &pysession, &pydepsgraph))
    return NULL;

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  PointerRNA depsgraphptr;
  RNA_pointer_create(NULL, &RNA_Depsgraph, (ID *)PyLong_AsVoidPtr(pydepsgraph), &depsgraphptr);
  BL::Depsgraph b_depsgraph(depsgraphptr);

  python_thread_state_save(&session->python_thread_state);

  session->render(b_depsgraph);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *draw_func(PyObject * /*self*/, PyObject *args) {
  PyObject *pysession, *pygraph, *pyv3d, *pyrv3d;

  if (!PyArg_ParseTuple(args, "OOOO", &pysession, &pygraph, &pyv3d, &pyrv3d))
    return NULL;

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  if (PyLong_AsVoidPtr(pyrv3d)) {
    /* 3d view drawing */
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    session->draw(viewport[2], viewport[3]);
  }

  Py_RETURN_NONE;
}

static PyObject *reset_func(PyObject * /*self*/, PyObject *args) {
  PyObject *pysession, *pydata, *pydepsgraph;

  if (!PyArg_ParseTuple(args, "OOO", &pysession, &pydata, &pydepsgraph))
    return NULL;

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  PointerRNA dataptr;
  RNA_main_pointer_create((Main *)PyLong_AsVoidPtr(pydata), &dataptr);
  BL::BlendData b_data(dataptr);

  PointerRNA depsgraphptr;
  RNA_pointer_create(NULL, &RNA_Depsgraph, PyLong_AsVoidPtr(pydepsgraph), &depsgraphptr);
  BL::Depsgraph b_depsgraph(depsgraphptr);

  python_thread_state_save(&session->python_thread_state);

  session->reset_session(b_data, b_depsgraph);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *sync_func(PyObject * /*self*/, PyObject *args) {
  PyObject *pysession, *pydepsgraph;

  if (!PyArg_ParseTuple(args, "OO", &pysession, &pydepsgraph))
    return NULL;

  BlenderSession *session = (BlenderSession *)PyLong_AsVoidPtr(pysession);

  PointerRNA depsgraphptr;
  RNA_pointer_create(NULL, &RNA_Depsgraph, PyLong_AsVoidPtr(pydepsgraph), &depsgraphptr);
  BL::Depsgraph b_depsgraph(depsgraphptr);

  python_thread_state_save(&session->python_thread_state);

  session->synchronize(b_depsgraph);

  python_thread_state_restore(&session->python_thread_state);

  Py_RETURN_NONE;
}

static PyObject *available_devices_func(PyObject * /*self*/, PyObject *args) {
  const char *type_name;
  if (!PyArg_ParseTuple(args, "s", &type_name)) {
    return NULL;
  }

  DeviceType type = Device::type_from_string(type_name);
  uint mask = (type == DEVICE_NONE) ? DEVICE_MASK_ALL : DEVICE_MASK(type);
  mask |= DEVICE_MASK_CPU;

  vector<DeviceInfo> devices = Device::available_devices(mask);
  PyObject *ret = PyTuple_New(devices.size());

  for (size_t i = 0; i < devices.size(); i++) {
    DeviceInfo &device = devices[i];
    string type_name = Device::string_from_type(device.type);
    PyObject *device_tuple = PyTuple_New(3);
    PyTuple_SET_ITEM(device_tuple, 0, PyUnicode_FromString(device.description.c_str()));
    PyTuple_SET_ITEM(device_tuple, 1, PyUnicode_FromString(type_name.c_str()));
    PyTuple_SET_ITEM(device_tuple, 2, PyUnicode_FromString(device.id.c_str()));
    PyTuple_SET_ITEM(ret, i, device_tuple);
  }

  return ret;
}

static PyObject *system_info_func(PyObject * /*self*/, PyObject * /*value*/) {
  string system_info = Device::device_capabilities();
  return PyUnicode_FromString(system_info.c_str());
}

static bool image_parse_filepaths(PyObject *pyfilepaths, vector<string> &filepaths) {
  if (PyUnicode_Check(pyfilepaths)) {
    const char *filepath = PyUnicode_AsUTF8(pyfilepaths);
    filepaths.push_back(filepath);
    return true;
  }

  PyObject *sequence = PySequence_Fast(pyfilepaths,
                                       "File paths must be a string or sequence of strings");
  if (sequence == NULL) {
    return false;
  }

  for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(sequence); i++) {
    PyObject *item = PySequence_Fast_GET_ITEM(sequence, i);
    const char *filepath = PyUnicode_AsUTF8(item);
    if (filepath == NULL) {
      PyErr_SetString(PyExc_ValueError, "File paths must be a string or sequence of strings.");
      Py_DECREF(sequence);
      return false;
    }
    filepaths.push_back(filepath);
  }
  Py_DECREF(sequence);

  return true;
}


static PyObject *debug_flags_update_func(PyObject * /*self*/, PyObject *args) {
  PyObject *pyscene;
  if (!PyArg_ParseTuple(args, "O", &pyscene)) {
    return NULL;
  }

  PointerRNA sceneptr;
  RNA_id_pointer_create((ID *)PyLong_AsVoidPtr(pyscene), &sceneptr);
  BL::Scene b_scene(sceneptr);

  if (debug_flags_sync_from_scene(b_scene)) {
    VLOG(2) << "Tagging device list for update.";
    Device::tag_update();
  }

  VLOG(2) << "Debug flags set to:\n" << DebugFlags();

  debug_flags_set = true;

  Py_RETURN_NONE;
}

static PyObject *debug_flags_reset_func(PyObject * /*self*/, PyObject * /*args*/) {
  if (debug_flags_reset()) {
    VLOG(2) << "Tagging device list for update.";
    Device::tag_update();
  }
  if (debug_flags_set) {
    VLOG(2) << "Debug flags reset to:\n" << DebugFlags();
    debug_flags_set = false;
  }
  Py_RETURN_NONE;
}

static PyObject *set_resumable_chunk_func(PyObject * /*self*/, PyObject *args) {
  int num_resumable_chunks, current_resumable_chunk;
  if (!PyArg_ParseTuple(args, "ii", &num_resumable_chunks, &current_resumable_chunk)) {
    Py_RETURN_NONE;
  }

  if (num_resumable_chunks <= 0) {
    fprintf(stderr, "Cycles: Bad value for number of resumable chunks.\n");
    abort();
    Py_RETURN_NONE;
  }
  if (current_resumable_chunk < 1 || current_resumable_chunk > num_resumable_chunks) {
    fprintf(stderr, "Cycles: Bad value for current resumable chunk number.\n");
    abort();
    Py_RETURN_NONE;
  }

  VLOG(1) << "Initialized resumable render: "
          << "num_resumable_chunks=" << num_resumable_chunks << ", "
          << "current_resumable_chunk=" << current_resumable_chunk;
  BlenderSession::num_resumable_chunks = num_resumable_chunks;
  BlenderSession::current_resumable_chunk = current_resumable_chunk;

  printf("Cycles: Will render chunk %d of %d\n", current_resumable_chunk, num_resumable_chunks);

  Py_RETURN_NONE;
}

static PyObject *set_resumable_chunk_range_func(PyObject * /*self*/, PyObject *args) {
  int num_chunks, start_chunk, end_chunk;
  if (!PyArg_ParseTuple(args, "iii", &num_chunks, &start_chunk, &end_chunk)) {
    Py_RETURN_NONE;
  }

  if (num_chunks <= 0) {
    fprintf(stderr, "Cycles: Bad value for number of resumable chunks.\n");
    abort();
    Py_RETURN_NONE;
  }
  if (start_chunk < 1 || start_chunk > num_chunks) {
    fprintf(stderr, "Cycles: Bad value for start chunk number.\n");
    abort();
    Py_RETURN_NONE;
  }
  if (end_chunk < 1 || end_chunk > num_chunks) {
    fprintf(stderr, "Cycles: Bad value for start chunk number.\n");
    abort();
    Py_RETURN_NONE;
  }
  if (start_chunk > end_chunk) {
    fprintf(stderr, "Cycles: End chunk should be higher than start one.\n");
    abort();
    Py_RETURN_NONE;
  }

  VLOG(1) << "Initialized resumable render: "
          << "num_resumable_chunks=" << num_chunks << ", "
          << "start_resumable_chunk=" << start_chunk << "end_resumable_chunk=" << end_chunk;
  BlenderSession::num_resumable_chunks = num_chunks;
  BlenderSession::start_resumable_chunk = start_chunk;
  BlenderSession::end_resumable_chunk = end_chunk;

  printf("Cycles: Will render chunks %d to %d of %d\n", start_chunk, end_chunk, num_chunks);

  Py_RETURN_NONE;
}

static PyObject *clear_resumable_chunk_func(PyObject * /*self*/, PyObject * /*value*/) {
  VLOG(1) << "Clear resumable render";
  BlenderSession::num_resumable_chunks = 0;
  BlenderSession::current_resumable_chunk = 0;

  Py_RETURN_NONE;
}

static PyObject *enable_print_stats_func(PyObject * /*self*/, PyObject * /*args*/) {
  BlenderSession::print_render_stats = true;
  Py_RETURN_NONE;
}

static PyObject *get_device_types_func(PyObject * /*self*/, PyObject * /*args*/) {
  vector<DeviceType> device_types = Device::available_types();
  bool has_cuda = false, has_optix = false, has_opencl = false;
  foreach (DeviceType device_type, device_types) {
    has_cuda |= (device_type == DEVICE_CUDA);
    has_optix |= (device_type == DEVICE_OPTIX);
    has_opencl |= (device_type == DEVICE_OPENCL);
  }
  PyObject *list = PyTuple_New(3);
  PyTuple_SET_ITEM(list, 0, PyBool_FromLong(has_cuda));
  PyTuple_SET_ITEM(list, 1, PyBool_FromLong(has_optix));
  PyTuple_SET_ITEM(list, 2, PyBool_FromLong(has_opencl));
  return list;
}

static PyMethodDef methods[] = {
    {"init", init_func, METH_VARARGS, ""},
    {"exit", exit_func, METH_VARARGS, ""},
    {"create", create_func, METH_VARARGS, ""},
    {"free", free_func, METH_O, ""},
    {"render", render_func, METH_VARARGS, ""},
    {"bake", bake_func, METH_VARARGS, ""},
    {"draw", draw_func, METH_VARARGS, ""},
    {"sync", sync_func, METH_VARARGS, ""},
    {"reset", reset_func, METH_VARARGS, ""},
    {"available_devices", available_devices_func, METH_VARARGS, ""},
    {"system_info", system_info_func, METH_NOARGS, ""},

    /* Debugging routines */
    {"debug_flags_update", debug_flags_update_func, METH_VARARGS, ""},
    {"debug_flags_reset", debug_flags_reset_func, METH_NOARGS, ""},

    /* Statistics. */
    {"enable_print_stats", enable_print_stats_func, METH_NOARGS, ""},

    /* Resumable render */
    {"set_resumable_chunk", set_resumable_chunk_func, METH_VARARGS, ""},
    {"set_resumable_chunk_range", set_resumable_chunk_range_func, METH_VARARGS, ""},
    {"clear_resumable_chunk", clear_resumable_chunk_func, METH_NOARGS, ""},

    /* Compute Device selection */
    {"get_device_types", get_device_types_func, METH_VARARGS, ""},

    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_steam",
    "Blender steam render integration",
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    NULL,
};

CCL_NAMESPACE_END

void *CCL_python_module_init() {
  PyObject *mod = PyModule_Create(&ccl::module);

  PyModule_AddObject(mod, "with_osl", Py_False);
  Py_INCREF(Py_False);
  PyModule_AddStringConstant(mod, "osl_version", "unknown");
  PyModule_AddStringConstant(mod, "osl_version_string", "unknown");

#ifdef WITH_CYCLES_DEBUG
  PyModule_AddObject(mod, "with_steam_debug", Py_True);
  Py_INCREF(Py_True);
#else
  PyModule_AddObject(mod, "with_steam_debug", Py_False);
  Py_INCREF(Py_False);
#endif

  PyModule_AddObject(mod, "with_network", Py_False);
  Py_INCREF(Py_False);

#ifdef WITH_EMBREE
  PyModule_AddObject(mod, "with_embree", Py_True);
  Py_INCREF(Py_True);
#else  /* WITH_EMBREE */
  PyModule_AddObject(mod, "with_embree", Py_False);
  Py_INCREF(Py_False);
#endif /* WITH_EMBREE */

  return (void *)mod;
}
