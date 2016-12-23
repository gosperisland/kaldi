cmake_minimum_required(VERSION 2.6)
project (openfst)


if (CMAKE_COMPILER_IS_GNUCXX)
        
	set(DOUBLE_PRECISION 0)
        #set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
        if (NOT APPLE) #DEBUG
            add_definitions ( -msse -msse2 -Wall -pthread -DKALDI_DOUBLEPRECISION=${DOUBLE_PRECISION} -Wno-sign-compare -Wno-unused-local-typedefs -Winit-self -DHAVE_EXECINFO_H=1 -rdynamic -DHAVE_CXXABI_H -DHAVE_ATLAS) 
        endif ()

  
endif ()








