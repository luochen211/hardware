# main 组件 CMakeLists.txt
idf_component_register(SRCS "main.c" "wifi_connect.c"
                       INCLUDE_DIRS ".")
