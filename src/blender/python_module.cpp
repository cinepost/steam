#include <boost/python.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>

#include <string>
#include <iostream>

#include "python_module.h"
#include "renderer.h"
#include "session.h"
//#include "blender/gpu_texture.h"

using namespace boost::python;

namespace steam { namespace py {

void init(const std::string &path, const std::string &user_path, bool headless) {
	BlenderSession::headless = headless;
}

void exit() {

}

bool with_osl() {
	return false;
}

bool with_embree() {
#ifdef
	return true;
#else
	return false;
#endif // embree
}

BlenderSession* create_session() {
	/* create session */
  	BlenderSession *session;

	if (rv3d) {
    	/* interactive viewport session */
    	int width = region.width();
    	int height = region.height();

    	session = new BlenderSession(engine, preferences, data, v3d, rv3d, width, height);
  	} else {
    	/* offline session or preview render */
    	session = new BlenderSession(engine, preferences, data, preview_osl);
  	}

  	return session;
}

void free_session(BlenderSession *session) {
  delete session;
}

void export_Renderer();

BOOST_PYTHON_MODULE(_steam){
	def("init", init);
	def("exit", exit);
	def("with_osl", with_osl);
	def("with_embree", with_embree);
	def("create", create_session);
	def("free", free_session);
  	export_Renderer();
  	//export_GPU_Texture();
  	//export_GPU_TextureManager();
}


}}
