cmake_minimum_required(VERSION 2.6)
project (openfst)


if (CMAKE_COMPILER_IS_GNUCXX)
        

        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
        if (NOT APPLE)
            add_definitions (-g -O2 -MT -Wall -MD -MP -MF -fPIC -DPIC) # todo: should we use CMAKE_C_FLAGS for these?
        endif ()

  
endif ()








