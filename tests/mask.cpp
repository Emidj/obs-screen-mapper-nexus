// SPDX-License-Identifier: GPL-2.0-or-later
#include "mask-fixtures.hpp"
#include <cstdio>
#include <cstdlib>
static void check(bool ok,const char *label) { std::printf("%s: %s\n",ok?"PASS":"FAIL",label); if(!ok) std::exit(1); }
int main(int argc,char **argv)
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    check(argc==2,"mask fixture directory supplied"); const std::string directory=argv[1];
    check(create_mask_fixtures(directory),"PNG and SVG fixtures created natively");
    auto png=mapping::load_mask_image(directory+"/máscara-ñ.png");
    check(png.error.empty() && png.width==8 && png.height==8,"PNG loads through Unicode path");
    check(png.pixels[3]==255 && png.pixels[7*4+3]==0,"PNG alpha preserved");
    auto gray=mapping::load_mask_image(directory+"/gray.png");
    check(gray.error.empty() && gray.pixels[0]==128 && gray.pixels[3]==128,"PNG uses straight RGBA without premultiplication");
    auto svg=mapping::load_mask_image(directory+"/half.svg");
    check(svg.error.empty() && svg.width==1024 && svg.height==1024,"SVG rasterized with native Direct2D");
    check(svg.pixels[(512*1024+128)*4+3]==255 && svg.pixels[(512*1024+896)*4+3]==0,"SVG viewBox path and transparency preserved");
    auto partial=mapping::load_mask_image(directory+"/partial.svg");
    check(partial.error.empty() && partial.pixels[0]>=253 && std::abs(int(partial.pixels[3])-128)<=1,"SVG partial alpha converted to straight RGBA");
    check(!mapping::load_mask_image(directory+"/missing.png").error.empty(),"missing mask reports error");
    check(!mapping::load_mask_image(directory+"/bad.png").error.empty(),"corrupt PNG reports error");
    check(!mapping::load_mask_image(directory+"/bad.svg").error.empty(),"corrupt SVG reports error");
    check(!mapping::load_mask_image(directory+"/bad.gif").error.empty(),"unsupported format reports error");
    check(fixture_png(directory+"/oversize.png",4097,1,std::vector<uint8_t>(4097*4,255)),"oversized PNG fixture created");
    check(!mapping::load_mask_image(directory+"/oversize.png").error.empty(),"oversized PNG rejected before pixel allocation");
    std::puts("PASS: native mask loader validation complete");
}
