{
    "NAME" : "spec2k-mesa",
    "CFILES" : "accum.c alphabuf.c alpha.c api1.c api2.c attrib.c bitmap.c blend.c clip.c colortab.c context.c copypix.c depth.c dlist.c drawpix.c enable.c eval.c feedback.c fog.c get.c hash.c image.c light.c lines.c logic.c masking.c matrix.c mesa4.c misc.c mmath.c osmesa.c pb.c pixel.c pointers.c points.c polygon.c quads.c rastpos.c readpix.c rect.c scissor.c shade.c span.c stencil.c teximage.c texobj.c texstate.c texture.c triangle.c varray.c vb.c vbfill.c vbrender.c vbxform.c winpos.c xform.c",
    "CFLAGS" : " -I../../../ -DSPEC_CPU_LP64  -I../../../GL/   -lm ",
    "CCURED_FLAGS" : "",
    "LLVM_LD_FLAGS" : " -internalize -ipsccp -globalopt -constmerge -deadargelim -instcombine -basiccg -inline -prune-eh -globalopt -globaldce -basiccg -argpromotion -instcombine -jump-threading -domtree -domfrontier -scalarrepl -basiccg -globalsmodref-aa -domtree -loops -loopsimplify -domfrontier -scalar-evolution  -licm -memdep -gvn -memdep -memcpyopt -dse -instcombine -jump-threading -domtree -domfrontier -mem2reg -simplifycfg -globaldce -instcombine -instcombine -simplifycfg -adce -globaldce -preverify -domtree -verify ",
    "COPY_INPUT" : " ../../../numbers ",
#    "FAST_INPUT" : "",
#    "SLOW_INPUT" : "",
    "FAST_COMMANDLINE" : " -frames 1000 -meshfile ../../../mesa.in -ppmfile ../../../temp.ppm",
    "SLOW_COMMANDLINE" : " -frames 1000 -meshfile ../../../mesa.in -ppmfile ../../../temp.ppm",
    "SIM_COMMANDLINE" :  " -frames 500 -meshfile ../../../mesa.in -ppmfile ../../../temp.ppm",

#    "SLOW_COMMANDLINE" : "../../ref-inp.in",
    
    "SIM_EXP_COMMANDLINE" : " -frames 500 -meshfile ../../../mesa.in -ppmfile ../../../temp.ppm",
    
    "SIM_COPY_INPUT" : ["numbers", "temp.ppm", "mesa.in"],
    "SIM_NEW_COMMANDLINE" : " -frames 500 -meshfile inputs/mesa.in -ppmfile inputs/temp.ppm",

    }
    
