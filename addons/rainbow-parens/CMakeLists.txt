kate_add_plugin(rainbowparens)
target_compile_definitions(rainbowparens PRIVATE TRANSLATION_DOMAIN="rainbowparens")
target_link_libraries(rainbowparens PRIVATE KF${KF_MAJOR_VERSION}::TextEditor)

target_sources(
  rainbowparens
  PRIVATE
    rainbowparens_plugin.cpp
)

