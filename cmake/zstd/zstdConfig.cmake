# Lets libchdr find RT64's zstd (lib/rt64/src/contrib/zstd) as if it were an installed package, so the
# build has a single copy of zstd.
if(NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static INTERFACE IMPORTED)
    target_link_libraries(zstd::libzstd_static INTERFACE libzstd_static)
    target_include_directories(zstd::libzstd_static INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../../lib/rt64/src/contrib/zstd/lib")
endif()
set(zstd_FOUND TRUE)
