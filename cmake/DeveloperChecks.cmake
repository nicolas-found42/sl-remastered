# Applied after target declarations so test targets share one assertion policy.
option(SLRR_ENABLE_SANITIZERS "Instrument native code for diagnostics; not parity or release evidence" OFF)

get_property(slrr_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(slrr_target IN LISTS slrr_targets)
  get_target_property(slrr_type ${slrr_target} TYPE)
  if(NOT slrr_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
    continue()
  endif()
  if(SLRR_ENABLE_SANITIZERS)
    target_compile_options(${slrr_target} PRIVATE
      -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -fno-omit-frame-pointer)
    if(NOT slrr_type STREQUAL "OBJECT_LIBRARY" AND NOT slrr_type STREQUAL "STATIC_LIBRARY")
      target_link_options(${slrr_target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    endif()
  endif()
  if(slrr_type STREQUAL "EXECUTABLE")
    get_target_property(slrr_sources ${slrr_target} SOURCES)
    foreach(slrr_source IN LISTS slrr_sources)
      if(slrr_source MATCHES "(^|/)tests/.*\\.(cpp|cc|cxx)$")
        target_compile_options(${slrr_target} PRIVATE -UNDEBUG)
        break()
      endif()
    endforeach()
  endif()
endforeach()

# These targets compile only their listed native source and standard-library
# headers. Their tests do not open game assets, create windows, or play audio.
add_custom_target(portable-tests DEPENDS
  assertions_enabled_test owner_links_test instance_ids_test physics_body_test)
set_tests_properties(assertions_enabled_unit owner_links_unit instance_ids_unit physics_body_unit
  PROPERTIES LABELS portable RUN_SERIAL TRUE)
