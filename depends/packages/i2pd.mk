package=i2pd
#$(package)_version=2.55.0
#$(package)_download_path=https://github.com/pocketnetteam/i2pd/releases/download/$($(package)_version)/
$(package)_version=2.57.0
$(package)_download_path=https://github.com/PurpleI2P/i2pd/archive/$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
#$(package)_sha256_hash=f5792a1c0499143c716663e90bfb105aaa7ec47d1c4550b5f90ebfc25da00c6c
$(package)_sha256_hash=e2327f816d92a369eaaf9fd1661bc8b350495199e2f2cb4bfd4680107cd1d4b4
$(package)_dependencies=boost openssl zlib miniupnpc

define $(package)_set_vars
$(package)_build_opts=USE_UPNP=yes DEBUG=no USE_STATIC=yes
#$(package)_build_opts_linux=USE_STATIC=yes

$(package)_cxxflags=-I$($($(1)_type)_prefix)/include
$(package)_cxxflags_darwin=-std=c++17 -fvisibility=hidden

#$(package)_cppflags=-I$($($(1)_type)_prefix)/include

$(package)_ldlibs=-L$($($(1)_type)_prefix)/lib -lssl -lcrypto -lz -lminiupnpc -lpthread

ifeq ($(host_arch),arm)
  $(package)_ldlibs+=-lboost_program_options-mt-a64
else ifeq ($(host_os),mingw32)
  $(package)_build_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CXX="$($(package)_cxx)" WINDRES="$($(package)_windres)"
  $(package)_ldlibs+=-lboost_program_options-mt-s-x64 -lboost_filesystem-mt-s-x64
else
  $(package)_ldlibs+=-lboost_program_options-mt-x64
endif

ifeq ($(build_os),darwin)
  SED_INP_OPT=-i ''
else
  SED_INP_OPT=-i
endif
endef

define $(package)_preprocess_cmds
  sed $(SED_INP_OPT) "s|NEEDED_CXXFLAGS += -std=c++20|NEEDED_CXXFLAGS += -std=c++2a|" Makefile.mingw
endef

define $(package)_build_cmds
  $(MAKE) $($(package)_build_opts) CXXFLAGS="$($(package)_cxxflags)" LDLIBS="$($(package)_ldlibs)" libi2pd.a libi2pdclient.a libi2pdlang.a
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib && \
  cp libi2pd.a $($(package)_staging_prefix_dir)/lib && \
  cp libi2pdclient.a $($(package)_staging_prefix_dir)/lib && \
  cp libi2pdlang.a $($(package)_staging_prefix_dir)/lib && \
  mkdir -p $($(package)_staging_prefix_dir)/include/libi2pd && \
  cp libi2pd/*.h* $($(package)_staging_prefix_dir)/include/libi2pd/ && \
  cp libi2pd_client/*.h $($(package)_staging_prefix_dir)/include/libi2pd/ && \
  cp i18n/*.h $($(package)_staging_prefix_dir)/include/libi2pd/ && \
  sed $(SED_INP_OPT) "s/LogPrint/I2PLogPrint/g" $($(package)_staging_prefix_dir)/include/libi2pd/*.h
endef
