#
# Copyright 2011-2013 Blender Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# <pep8 compliant>

from bl_operators.presets import AddPresetBase
from bpy.types import Operator


class AddPresetIntegrator(AddPresetBase, Operator):
    '''Add an Integrator Preset'''
    bl_idname = "render.steam_integrator_preset_add"
    bl_label = "Add Integrator Preset"
    preset_menu = "CYCLES_PT_integrator_presets"

    preset_defines = [
        "steam = bpy.context.scene.steam"
    ]

    preset_values = [
        "steam.max_bounces",
        "steam.diffuse_bounces",
        "steam.glossy_bounces",
        "steam.transmission_bounces",
        "steam.volume_bounces",
        "steam.transparent_max_bounces",
        "steam.caustics_reflective",
        "steam.caustics_refractive",
        "steam.blur_glossy"
    ]

    preset_subdir = "steam/integrator"


class AddPresetSampling(AddPresetBase, Operator):
    '''Add a Sampling Preset'''
    bl_idname = "render.steam_sampling_preset_add"
    bl_label = "Add Sampling Preset"
    preset_menu = "CYCLES_PT_sampling_presets"

    preset_defines = [
        "steam = bpy.context.scene.steam"
    ]

    preset_values = [
        "steam.samples",
        "steam.preview_samples",
        "steam.aa_samples",
        "steam.preview_aa_samples",
        "steam.diffuse_samples",
        "steam.glossy_samples",
        "steam.transmission_samples",
        "steam.ao_samples",
        "steam.mesh_light_samples",
        "steam.subsurface_samples",
        "steam.volume_samples",
        "steam.use_square_samples",
        "steam.progressive",
        "steam.seed",
        "steam.sample_clamp_direct",
        "steam.sample_clamp_indirect",
        "steam.sample_all_lights_direct",
        "steam.sample_all_lights_indirect",
    ]

    preset_subdir = "steam/sampling"


classes = (
    AddPresetIntegrator,
    AddPresetSampling,
)


def register():
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)


def unregister():
    from bpy.utils import unregister_class
    for cls in classes:
        unregister_class(cls)


if __name__ == "__main__":
    register()
