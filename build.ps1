param(
    [string]$Zig = 'C:/Tools/zig-0.15.2\zig.exe',
    [string]$ObsSdk = 'C:/SDK/obs-studio-32.2.2',
    [string]$ObsInstall = 'C:\Program Files\obs-studio',
    [string]$ResourceCompiler = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\rc.exe'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& (Join-Path $root 'translations\generate.ps1')
$build = Join-Path $root 'build'
$bin = Join-Path $root 'dist\obs-screen-mapper\bin\64bit'
New-Item -ItemType Directory -Force $build,$bin,(Join-Path $root 'dist\obs-screen-mapper\data') | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build 'zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $build 'zig-local'
# Import only the public libobs symbols used by this phase; no third-party binaries copied.
$exports = @(
 'obs_register_source_s','obs_data_get_int','obs_data_set_default_int',
 'obs_properties_create','obs_properties_add_int','obs_properties_add_text',
 'obs_startup','obs_shutdown','obs_open_module','obs_init_module','obs_source_get_type','obs_source_is_group','obs_property_list_item_disable',
 'obs_source_create','obs_source_get_width','obs_source_get_height','obs_source_properties',
 'obs_properties_get','obs_properties_destroy','obs_data_create','obs_data_set_int',
 'obs_source_update','obs_source_video_render','obs_save_source','obs_load_source',
 'obs_source_release','obs_data_release',
 'obs_source_get_ref','obs_source_remove_active_child','obs_get_source_by_uuid',
 'obs_source_removed','obs_source_get_output_flags','obs_source_add_active_child',
 'obs_data_set_default_string','obs_data_get_string','gs_matrix_push','gs_matrix_scale3f','gs_matrix_pop',
 'obs_properties_add_list','obs_property_list_add_string','obs_source_get_name','obs_source_get_uuid','obs_enum_sources',
 'obs_data_set_string','obs_source_enum_full_tree','obs_source_remove','obs_source_set_name',
 'obs_scene_create','obs_scene_get_source','obs_scene_add','obs_scene_release',
 'obs_add_data_path','obs_reset_video','obs_get_base_effect','gs_effect_set_vec4',
 'gs_effect_get_param_by_name','gs_effect_loop','gs_draw_sprite','obs_enter_graphics','obs_leave_graphics',
 'gs_texrender_create','gs_stagesurface_create','gs_texrender_begin','gs_clear','gs_ortho',
 'gs_blend_state_push','gs_blend_function','gs_blend_state_pop','gs_texrender_end',
 'gs_stage_texture','gs_texrender_get_texture','gs_stagesurface_map','gs_stagesurface_unmap',
 'gs_stagesurface_destroy','gs_texrender_destroy',
 'obs_data_set_default_bool','obs_data_set_default_double','obs_data_has_user_value',
 'obs_data_get_double','obs_data_get_bool','obs_data_set_double','obs_data_set_bool',
 'obs_data_get_array','obs_data_array_count','obs_data_array_item','obs_data_array_release',
 'obs_data_array_create','obs_data_array_push_back','obs_data_set_array',
 'gs_texrender_reset','gs_effect_set_texture','gs_render_start','gs_render_stop','gs_texcoord','gs_vertex2f',
 'gs_render_save','gs_load_vertexbuffer','gs_load_indexbuffer','gs_draw','gs_vertexbuffer_destroy',
 'obs_property_list_clear','obs_property_set_enabled','obs_property_name','obs_property_set_modified_callback',
 'obs_properties_add_bool','obs_properties_add_float','obs_source_get_settings',
 'obs_property_modified','obs_properties_apply_settings','gs_matrix_translate3f',
 'obs_data_create_from_json','obs_data_get_json','obs_property_list_add_int','obs_property_set_description',
 'gs_texture_create','gs_texture_destroy','gs_effect_create','gs_effect_destroy','gs_effect_set_float',
 'bfree','blog','obs_properties_add_path','obs_properties_add_button2','obs_property_button_clicked',
 'obs_scene_duplicate','obs_scene_from_source','obs_group_from_source','obs_scene_enum_items','obs_sceneitem_addref','obs_sceneitem_release','obs_sceneitem_get_source',
 'obs_sceneitem_get_info2','obs_sceneitem_set_info2','obs_sceneitem_get_crop','obs_sceneitem_set_crop','obs_sceneitem_visible','obs_sceneitem_set_visible','obs_sceneitem_locked','obs_sceneitem_set_locked',
 'obs_sceneitem_set_scale_filter','obs_sceneitem_get_scale_filter','obs_sceneitem_set_blending_method','obs_sceneitem_get_blending_method','obs_sceneitem_set_blending_mode','obs_sceneitem_get_blending_mode',
 'obs_sceneitem_get_order_position','obs_sceneitem_set_order_position','obs_sceneitem_force_update_transform','obs_sceneitem_get_draw_transform','obs_sceneitem_get_bounds_crop','gs_matrix_mul',
 'obs_source_create_private','obs_source_inc_showing','obs_source_dec_showing',
 'obs_display_create','obs_display_destroy','obs_display_resize','obs_display_add_draw_callback','obs_display_remove_draw_callback','obs_enum_scenes','obs_sceneitem_remove','gs_matrix_identity','gs_draw_sprite_subregion',
 'obs_properties_first','obs_property_next','obs_property_set_visible','obs_property_visible','obs_data_set_obj','obs_module_get_config_path','obs_get_locale','obs_set_locale','obs_property_description','obs_obj_is_private','obs_source_enum_filters','obs_source_filter_remove','obs_filter_get_parent','obs_source_filter_add','obs_source_enabled'
)
Set-Content (Join-Path $build 'obs.def') ("LIBRARY obs.dll`nEXPORTS`n" + ($exports -join "`n")) -Encoding ascii
Set-Content (Join-Path $build 'obsconfig.h') "#pragma once`n#define OBS_RELEASE_CANDIDATE 0`n#define OBS_BETA 0" -Encoding ascii
& $Zig dlltool -m i386:x86-64 -d (Join-Path $build 'obs.def') -l (Join-Path $build 'obs.lib')
if ($LASTEXITCODE) { throw 'Import library failed' }
$env:ZIG_LIB_DIR = Join-Path (Split-Path $Zig) 'lib'
$common = @('c++','-target','x86_64-windows-gnu','-std=c++17','-O2','-Wall','-Wextra','-Werror',"-I$ObsSdk\libobs","-I$build")
$native = @('-ld2d1','-ld3d11','-lole32','-lshlwapi','-luuid','-lxmllite','-luser32')
& $ResourceCompiler /nologo ("/fo"+(Join-Path $build 'mapper-version.res')) (Join-Path $root 'src\version.rc')
if ($LASTEXITCODE) { throw 'Version resource compilation failed' }
& $Zig @common -shared (Join-Path $root 'src\plugin.cpp') (Join-Path $build 'obs.lib') (Join-Path $build 'mapper-version.res') @native -o (Join-Path $bin 'obs-screen-mapper.dll')
if ($LASTEXITCODE) { throw 'Plugin compilation failed' }
& $Zig @common (Join-Path $root 'tests\lifecycle.cpp') (Join-Path $build 'obs.lib') -o (Join-Path $build 'lifecycle.exe')
if ($LASTEXITCODE) { throw 'Test compilation failed' }
& $Zig @common (Join-Path $root 'tests\gpu.cpp') (Join-Path $build 'obs.lib') @native -o (Join-Path $build 'gpu.exe')
if ($LASTEXITCODE) { throw 'GPU test compilation failed' }
& $Zig @common (Join-Path $root 'tests\polygon.cpp') -o (Join-Path $build 'polygon.exe')
if ($LASTEXITCODE) { throw 'Polygon test compilation failed' }
& (Join-Path $build 'polygon.exe') 2>&1 | Tee-Object (Join-Path $build 'polygon-validation.log')
if ($LASTEXITCODE) { throw 'Polygon geometry test failed' }
& $Zig @common (Join-Path $root 'tests\mesh.cpp') -o (Join-Path $build 'mesh.exe')
if ($LASTEXITCODE) { throw 'Mesh test compilation failed' }
& (Join-Path $build 'mesh.exe') 2>&1 | Tee-Object (Join-Path $build 'mesh-validation.log')
if ($LASTEXITCODE) { throw 'Mesh geometry test failed' }
& $Zig @common (Join-Path $root 'tests\mask.cpp') @native -o (Join-Path $build 'mask.exe')
if ($LASTEXITCODE) { throw 'Mask test compilation failed' }
& (Join-Path $build 'mask.exe') (Join-Path $build 'mask-fixtures') 2>&1 | Tee-Object (Join-Path $build 'mask-validation.log')
if ($LASTEXITCODE) { throw 'Native mask loader test failed' }
$previousPath = $env:PATH
try {
    $env:PATH = "$ObsInstall\bin\64bit;$previousPath"
    & (Join-Path $build 'lifecycle.exe') (Join-Path $bin 'obs-screen-mapper.dll') (Join-Path $root 'dist\obs-screen-mapper\data') 2>&1 | Tee-Object (Join-Path $build 'validation.log')
    if ($LASTEXITCODE) { throw "Lifecycle test failed: $LASTEXITCODE" }
    & (Join-Path $build 'gpu.exe') (Join-Path $bin 'obs-screen-mapper.dll') (Join-Path $root 'dist\obs-screen-mapper\data') (Join-Path $ObsInstall 'data\libobs') (Join-Path $build 'mask-fixtures') 2>&1 | Tee-Object (Join-Path $build 'gpu-validation.log')
    if ($LASTEXITCODE) { throw "GPU test failed: $LASTEXITCODE" }
} finally { $env:PATH = $previousPath }
Get-FileHash (Join-Path $bin 'obs-screen-mapper.dll') | Format-List


