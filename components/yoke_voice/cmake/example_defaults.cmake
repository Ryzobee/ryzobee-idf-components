# Pin the complete tested dependency set for bundled examples only.
if(DEFINED ENV{IDF_COMPONENT_CONSTRAINT_FILES} AND NOT "$ENV{IDF_COMPONENT_CONSTRAINT_FILES}" STREQUAL "")
    set(ENV{IDF_COMPONENT_CONSTRAINT_FILES} "$ENV{IDF_COMPONENT_CONSTRAINT_FILES};${CMAKE_CURRENT_LIST_DIR}/dependencies.constraints")
else()
    set(ENV{IDF_COMPONENT_CONSTRAINT_FILES} "${CMAKE_CURRENT_LIST_DIR}/dependencies.constraints")
endif()
