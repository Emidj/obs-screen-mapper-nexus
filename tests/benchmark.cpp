// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <graphics/vec4.h>
#include <callback/signal.h>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <fstream>
#include <algorithm>
#include "../src/zones.hpp"
#include "mask-fixtures.hpp"

using Clock=std::chrono::steady_clock;
static uint64_t now() {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());}
static void require(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static std::atomic<unsigned> inputRenders{0};
static const char *fixtureName(void*){return "Benchmark existing input";}
static void *fixtureCreate(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
static void fixtureDestroy(void*){}
static uint32_t fixtureWidth(void*){return 1920;}
static uint32_t fixtureHeight(void*){return 1080;}
static void fixtureRender(void*,gs_effect_t*) {
    ++inputRenders;auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
    for(int i=0;i<4;++i){vec4 c;vec4_set(&c,i==0||i==3?.25f:0,i==1||i==3?.25f:0,i==2?.25f:0,.5f);
        gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&c);gs_matrix_push();gs_matrix_translate3f(float(i%2)*960,float(i/2)*540,0);
        while(gs_effect_loop(effect,"Solid"))gs_draw_sprite(nullptr,0,960,540);gs_matrix_pop();}
}
struct Scenario {const char *name;int zones,width,height,geometry,views;};
struct UpdateProbe {std::atomic<uint64_t> tick{0};std::mutex mutex;std::condition_variable condition;bool pending=false,done=false;double milliseconds=0;};
static void tick(void *data,float){static_cast<UpdateProbe*>(data)->tick.store(now());}
static void updated(void *data,calldata_t*){auto *probe=static_cast<UpdateProbe*>(data);const auto end=now();std::lock_guard<std::mutex> lock(probe->mutex);if(probe->pending){probe->milliseconds=double(end-probe->tick.load())/1e6;probe->done=true;probe->pending=false;probe->condition.notify_one();}}
static double percentile(std::vector<double> values,double p){std::sort(values.begin(),values.end());return values[size_t(std::ceil(p*values.size()))-1];}
static uint64_t checksum(const std::vector<uint8_t> &pixels){uint64_t hash=1469598103934665603ULL;for(auto c:pixels){hash^=c;hash*=1099511628211ULL;}return hash;}
int main(int argc,char **argv){
    require(argc==7,"usage: benchmark DLL DATA OBS_ROOT REPORT LABEL REFERENCES");std::setvbuf(stdout,nullptr,_IONBF,0);
    require(obs_startup("en-US",nullptr,nullptr),"libobs startup");const std::string obs=argv[3],data=obs+"/data/libobs/",graphics=obs+"/bin/64bit/libobs-d3d11.dll";
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    obs_add_data_path(data.c_str());
#pragma clang diagnostic pop
    obs_video_info video{};video.graphics_module=graphics.c_str();video.fps_num=60;video.fps_den=1;video.base_width=1920;video.base_height=1080;video.output_width=1920;video.output_height=1080;video.output_format=VIDEO_FORMAT_RGBA;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;
    require(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"D3D11 initialized");obs_module_t *module=nullptr;require(obs_open_module(&module,argv[1],argv[2])==MODULE_SUCCESS&&obs_init_module(module),"module loaded");
    obs_source_info info{};info.id="mapper_benchmark_input";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;info.get_name=fixtureName;info.create=fixtureCreate;info.destroy=fixtureDestroy;info.get_width=fixtureWidth;info.get_height=fixtureHeight;info.video_render=fixtureRender;obs_register_source(&info);
    auto *input=obs_source_create(info.id,"Benchmark input",nullptr,nullptr);require(input!=nullptr,"existing input created");
    const Scenario scenarios[]={{"rect_1_1080",1,1920,1080,0,1},{"rect_6_1080",6,1920,1080,0,1},{"rect_24_1080",24,1920,1080,0,1},{"rect_96_1080",96,1920,1080,0,1},
        {"mesh_6_1080",6,1920,1080,1,1},{"mesh_24_1080",24,1920,1080,1,1},{"polygon_mesh_6_1080",6,1920,1080,2,1},{"polygon_mesh_24_1080",24,1920,1080,2,1},
        {"mesh_mask_24_1080",24,1920,1080,3,1},{"rect_6_2160",6,3840,2160,0,1},{"polygon_mesh_24_2160",24,3840,2160,2,1},{"two_views_mesh_24",24,1920,1080,1,2}};
    std::ofstream report(argv[4]);require(bool(report),"report opened");report<<"label,case,zones,width,height,views,vertices,cpu_p50_ms,cpu_p95_ms,gpu_p50_ms,gpu_p95_ms,cold_cpu_ms,tick_update_p50_ms,tick_update_p95_ms,input_renders,expected_renders,pixel_hash,pixel_diff,max_channel_diff\n";
    UpdateProbe probe;obs_add_tick_callback(tick,&probe);
    for(const auto &scenario:scenarios){
        auto *settings=obs_data_create();obs_data_set_int(settings,"canvas_width",scenario.width);obs_data_set_int(settings,"canvas_height",scenario.height);obs_data_set_string(settings,"input_uuid",obs_source_get_uuid(input));obs_data_set_string(settings,"distribution","repeat");
        mapping::Layout layout;layout.mode=mapping::Mode::Repeat;
        size_t vertices=0;
        for(int i=0;i<scenario.zones;++i){mapping::Zone zone;zone.id=std::to_string(i+1);zone.name="Zone "+zone.id;
            if(scenario.geometry){zone.mesh.enabled=true;mapping::resize_mesh(zone.mesh,8,6);zone.mesh.horizontal=.05f;zone.mesh.points[10].y=.01f;}
            if(scenario.geometry==2){zone.polygon.enabled=true;zone.polygon.vertices={{0,0,0,0},{1,0,1,0},{1,.75f,1,.75f},{.75f,.75f,.75f,.75f},{.75f,1,.75f,1},{0,1,0,1}};mapping::compile_polygon(zone.polygon);}
            if(scenario.geometry==3){zone.mask.kind=1;zone.mask.feather=.1f;}
            auto surface=mapping::compile_surface(zone.mesh,zone.polygon);vertices+=scenario.geometry?surface.triangles.size():4;layout.zones.push_back(std::move(zone));}
        mapping::arrange(layout);mapping::write(settings,layout);auto *source=obs_source_create("obs_screen_mapper",scenario.name,settings,nullptr);require(source!=nullptr,"mapper created");
        obs_enter_graphics();auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto *range=gs_timer_range_create();constexpr int batch=8,samples=120,warm=32;gs_timer_t *timers[batch]{};for(auto &timer:timers){timer=gs_timer_create();require(timer!=nullptr,"GPU timer created");}
        std::vector<double> cpu,gpu;unsigned before=inputRenders.load();double cold=0;
        auto render=[&](gs_timer_t *timer){gs_texrender_reset(target);require(gs_texrender_begin(target,scenario.width,scenario.height),"target begin");vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,float(scenario.width),0,float(scenario.height),-100,100);gs_matrix_identity();
            if(timer)gs_timer_begin(timer);const auto start=now();for(int view=0;view<scenario.views;++view)obs_source_video_render(source);const double elapsed=double(now()-start)/1e6;if(timer)gs_timer_end(timer);gs_texrender_end(target);return elapsed;};
        cold=render(nullptr);for(int i=0;i<warm;++i)render(nullptr);gs_flush();
        for(int i=0;i<samples;i+=batch){gs_timer_range_begin(range);for(auto *timer:timers)cpu.push_back(render(timer));gs_timer_range_end(range);gs_flush();bool disjoint=false;uint64_t frequency=0;require(gs_timer_range_get_data(range,&disjoint,&frequency)&&!disjoint&&frequency>0,"valid GPU timestamp range");
            for(auto *timer:timers){uint64_t ticks=0;require(gs_timer_get_data(timer,&ticks),"GPU timestamps collected");gpu.push_back(double(ticks)*1000/double(frequency));}}
        auto *stage=gs_stagesurface_create(scenario.width,scenario.height,GS_RGBA);gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t *pixels=nullptr;uint32_t stride=0;require(gs_stagesurface_map(stage,&pixels,&stride),"untimed pixel verification");std::vector<uint8_t> image(size_t(scenario.width)*scenario.height*4);
        for(int y=0;y<scenario.height;++y)std::copy(pixels+size_t(y)*stride,pixels+size_t(y)*stride+scenario.width*4,image.begin()+size_t(y)*scenario.width*4);
        gs_stagesurface_unmap(stage);gs_stagesurface_destroy(stage);for(auto *timer:timers)gs_timer_destroy(timer);gs_timer_range_destroy(range);gs_texrender_destroy(target);obs_leave_graphics();
        const auto reference=std::string(argv[6])+"/"+scenario.name+".png";int maxDifference=0;size_t difference=0;
        if(std::string(argv[5]).find("baseline")!=std::string::npos)require(fixture_png(reference,scenario.width,scenario.height,image),"baseline pixels stored");
        else {auto previous=mapping::load_mask_image(reference);require(previous.error.empty()&&previous.pixels.size()==image.size(),"baseline image available");for(size_t i=0;i<image.size();++i){int d=std::abs(int(image[i])-int(previous.pixels[i]));maxDifference=std::max(maxDifference,d);if(d)++difference;}require(difference==0,"optimized output exactly matches baseline pixels");}
        const unsigned captured=inputRenders.load()-before,expected=(1+warm+samples)*scenario.views;require(captured==expected,"input capture count remains one per view");
        std::vector<double> update;signal_handler_connect(obs_source_get_signal_handler(source),"update",updated,&probe);
        for(int i=0;i<36;++i){obs_data_set_string(settings,"selected_zone",i%2?"1":std::to_string(scenario.zones).c_str());{std::lock_guard<std::mutex> lock(probe.mutex);probe.done=false;probe.pending=true;}obs_source_update(source,settings);std::unique_lock<std::mutex> lock(probe.mutex);require(probe.condition.wait_for(lock,std::chrono::seconds(5),[&]{return probe.done;}),"update completed on video thread");if(i>=6)update.push_back(probe.milliseconds);}
        signal_handler_disconnect(obs_source_get_signal_handler(source),"update",updated,&probe);
        report<<argv[5]<<','<<scenario.name<<','<<scenario.zones<<','<<scenario.width<<','<<scenario.height<<','<<scenario.views<<','<<vertices<<','<<percentile(cpu,.5)<<','<<percentile(cpu,.95)<<','<<percentile(gpu,.5)<<','<<percentile(gpu,.95)<<','<<cold<<','<<percentile(update,.5)<<','<<percentile(update,.95)<<','<<captured<<','<<expected<<','<<checksum(image)<<','<<difference<<','<<maxDifference<<'\n';report.flush();
        std::printf("BENCH %s %s CPU p95 %.4f ms; GPU interval p95 %.4f ms; update p95 %.4f ms\n",argv[5],scenario.name,percentile(cpu,.95),percentile(gpu,.95),percentile(update,.95));
        obs_source_release(source);obs_data_release(settings);
    }
    obs_remove_tick_callback(tick,&probe);obs_source_release(input);obs_shutdown();std::puts("PASS: isolated performance benchmark complete");
}
