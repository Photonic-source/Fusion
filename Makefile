#
# EDuke32 Makefile for GNU Make
#

include Makefile.common


DUKE3D_SRC=game
DUKE3D_INC=$(DUKE3D_SRC)
DUKE3D_RSRC=rsrc
ENGINE_ROOT=engine
ENGINE_SRC=$(ENGINE_ROOT)
ENGINE_INC=$(ENGINE_ROOT)/inc
o=o
asm=nasm


# BUILD Engine

ENGINE_CFLAGS=-I$(ENGINE_SRC)

ENGINE_OBJ=$(DUKE3D_SRC)/eobj_win

ENGINE_OBJS=baselayer cache1d compat crc32 defs engine polymost hightile textfont smalltextfont kplib osd pragmas scriptfile mutex crc32 lzf_c lzf_d md4 mdsprite glbuild

ifeq (0,$(NOASM))
  ENGINE_OBJS+= a
else
  ENGINE_OBJS+= a-c
  ifneq (0,$(USE_ASM64))
    ENGINE_OBJS+= a64
  endif
endif

ENGINE_OBJS+= mmulti

ENGINE_OBJS+= winlayer

ENGINE_OBJS_EXP:=$(addprefix $(ENGINE_OBJ)/,$(addsuffix .$o,$(ENGINE_OBJS)))


# AudioLib

AUDIOLIB_OBJS=drivers fx_man multivoc mix mixst pitch vorbis driver_nosound

AUDIOLIB_ROOT=$(DUKE3D_SRC)/jaudiolib
AUDIOLIB_SRC=$(AUDIOLIB_ROOT)/src
AUDIOLIB_INC=$(AUDIOLIB_ROOT)/include

AUDIOLIB_CFLAGS+= -DHAVE_DS
AUDIOLIB_OBJS+= driver_directsound


AUDIOLIB_OBJ=$(AUDIOLIB_ROOT)/obj_win


AUDIOLIB_OBJS_EXP:=$(addprefix $(AUDIOLIB_OBJ)/,$(addsuffix .$o,$(AUDIOLIB_OBJS)))

# Duke Nukem 3D

ifneq (0,$(RELEASE))
    # Debugging disabled
    COMMONFLAGS += $(F_NO_STACK_PROTECTOR)
else
    # Debugging enabled
    ifneq (0,$(KRANDDEBUG))
        COMMONFLAGS += -fno-inline -fno-inline-functions -fno-inline-functions-called-once
    endif
endif

COMPILERFLAGS += -I$(DUKE3D_INC) -I$(ENGINE_INC) -I$(DUKE3D_SRC)/jmact -I$(AUDIOLIB_ROOT)/include

# Game/editor-specific linker options
DUKE3D_LINKERFLAGS=


EDUKE32 ?= fusion$(EXESUFFIX)

EDUKE32_TARGET:=$(EDUKE32)

DUKE3D_OBJ=$(DUKE3D_SRC)/obj_win

MACT_OBJ=util_lib file_lib control keyboard mouse mathutil scriplib animlib

DUKE3D_OBJS=game actors anim config gamedef gameexec gamevars global menus namesdyn player premap savegame sector rts osdfuncs osdcmds grpscan sounds $(MACT_OBJ)

ifneq (0,$(DISABLEINLINING))
    DUKE3D_OBJS+= game_inline actors_inline sector_inline
endif

DUKE3D_MISCDEPS=

# PLATFORM SPECIFIC SETTINGS

ifeq ($(PLATFORM),WINDOWS)
    COMMONFLAGS += -fno-pic
    LIBS += -lvorbisfile -lvorbis -logg -lcompat-from-mingw-w64
    DUKE3D_OBJS+= gameres winbits startwin.game

    LIBS+= -ldsound
    DUKE3D_OBJS+= music midi mpu401
endif


DUKE3D_OBJS_EXP:=$(addprefix $(DUKE3D_OBJ)/,$(addsuffix .$o,$(DUKE3D_OBJS)))


ifeq ($(PRETTY_OUTPUT),1)
.SILENT:
endif
.PHONY: veryclean clean all 

# TARGETS

all: start $(EDUKE32_TARGET) finish
ifneq (,$(EDUKE32_TARGET))
	@ls -l $(EDUKE32)
endif

start:
	$(BUILD_STARTED)

finish:
	$(BUILD_FINISHED)

$(EDUKE32): $(DUKE3D_OBJS_EXP) $(ENGINE_OBJS_EXP) $(AUDIOLIB_OBJS_EXP) $(DUKE3D_MISCDEPS)
	$(LINK_STATUS)
	$(RECIPE_IF) $(LINKER) -o $@ $^ $(COMMONFLAGS) $(LINKERFLAGS) $(DUKE3D_LINKERFLAGS) $(LIBDIRS) $(LIBS) $(STATICSTDCPP) $(RECIPE_RESULT_LINK)
ifneq ($(STRIP),)
	$(STRIP) $(EDUKE32)
endif

include Makefile.deps

# RULES

libcache1d$(DLLSUFFIX): $(ENGINE_SRC)/cache1d.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) -Wall -Wextra -DCACHE1D_COMPRESS_ONLY -shared -fPIC $< -o $@ $(RECIPE_RESULT_COMPILE)

%$(EXESUFFIX): $(DUKE3D_OBJ)/%.$o
	$(ONESTEP_STATUS)
	$(RECIPE_IF) $(LINKER) -o $@ $^ $(COMMONFLAGS) $(LINKERFLAGS) $(LIBDIRS) $(LIBS) $(RECIPE_RESULT_ONESTEP)

$(DUKE3D_OBJ)/%.$o: $(DUKE3D_SRC)/%.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/%.nasm
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(AS) $(ASFLAGS) $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/%.yasm
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(AS) $(ASFLAGS) $< -o $@ $(RECIPE_RESULT_COMPILE)

# Comment out the following rule to debug a-c.o
$(ENGINE_OBJ)/a-c.$o: $(ENGINE_SRC)/a-c.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(subst -O$(OPTLEVEL),-O2,$(subst $(CLANG_DEBUG_FLAGS),,$(COMMONFLAGS) $(COMPILERFLAGS))) $(ENGINE_CFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/%.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) $(ENGINE_CFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/%.m
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) $(ENGINE_CFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/%.cpp
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(CXX) $(CPPONLYFLAGS) $(COMMONFLAGS) $(COMPILERFLAGS) $(ENGINE_CFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/misc/%.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) $(ENGINE_CFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(ENGINE_SRC)/misc/%.rc
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(RC) -i $< -o $@ --include-dir=$(ENGINE_INC) --include-dir=$(ENGINE_SRC) $(RECIPE_RESULT_COMPILE)

$(ENGINE_OBJ)/%.$o: $(DUKE3D_RSRC)/%.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(AUDIOLIB_OBJ)/%.o: $(AUDIOLIB_SRC)/%.c | $(AUDIOLIB_OBJ)
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) $(AUDIOLIB_CFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(AUDIOLIB_OBJ):
	mkdir $(AUDIOLIB_OBJ)

$(DUKE3D_OBJ)/%.$o: $(DUKE3D_SRC)/%.m
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(DUKE3D_OBJ)/%.$o: $(DUKE3D_SRC)/%.cpp
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(CXX) $(CPPONLYFLAGS) $(COMMONFLAGS) $(COMPILERFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(DUKE3D_OBJ)/%.$o: $(DUKE3D_SRC)/jmact/%.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(DUKE3D_OBJ)/%.$o: $(DUKE3D_SRC)/misc/%.rc
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(RC) -i $< -o $@ --include-dir=$(ENGINE_INC) --include-dir=$(DUKE3D_SRC) $(RECIPE_RESULT_COMPILE)

$(DUKE3D_OBJ)/%.$o: $(DUKE3D_RSRC)/%.c
	$(COMPILE_STATUS)
	$(RECIPE_IF) $(COMPILER) $(COMMONFLAGS) $(COMPILERFLAGS) -Wno-pointer-sign -c $< -o $@ $(RECIPE_RESULT_COMPILE)

$(DUKE3D_RSRC)/game_banner.c: $(DUKE3D_RSRC)/game.bmp
	echo "#include <gdk-pixbuf/gdk-pixdata.h>" > $@
	echo "extern const GdkPixdata startbanner_pixdata;" >> $@
	gdk-pixbuf-csource --extern --struct --raw --name=startbanner_pixdata $^ | sed 's/load_inc//' >> $@

# PHONIES

clean: $(UNDO_REV)
	-rm -f $(EDUKE32) $(MAPSTER32) $(DUKE3D_OBJS_EXP) $(DUKE3D_EDITOR_OBJS_EXP) $(DUKE3D_MISCDEPS) $(DUKE3D_EDITOR_MISCDEPS) core*

veryclean: clean
	-rm -f $(ENGINE_EDITOR_OBJS_EXP) $(ENGINE_OBJS_EXP) $(AUDIOLIB_OBJS_EXP) $(DUKE3D_RSRC)/*banner* $(EBACKTRACEDLL) installer/*.exe

