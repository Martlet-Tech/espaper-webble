set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_SRCS "your_code.c")  # 替换为您的 C 源文件名
set(COMPONENT_REQUIRES spiffs)

file(GLOB_RECURSE HTML_FILES "*.html")

spiffs_create_partition_image(spiffs ${HTML_FILES})
