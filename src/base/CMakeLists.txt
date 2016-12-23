cmake_minimum_required(VERSION 2.6)
project (base)
set(SOURCES_files_sources
	io-funcs.cc       
	kaldi-error.cc       
	kaldi-math.cc       
	kaldi-utils.cc
	io-funcs-test.cc  
	kaldi-error-test.cc  
	kaldi-math-test.cc  
	timer-test.cc
	  )
source_group(sources FILES ${SOURCES_files_sources})
source_group(include FILES ${SOURCES_files_include})
set(SOURCES
    ${SOURCES_files_sources}
    ${SOURCES_files_include}
)



#temporary. will be set by configure, on the root
set(FSTROOT ../../tools/openfst)

  include_directories(
    ..
    ${FSTROOT}/include
  )



add_library(base ${SOURCES})

