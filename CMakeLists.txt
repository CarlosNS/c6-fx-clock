cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Shown in the web page and checked by OTA updates
set(PROJECT_VER "3.0.0-beta.7")
# The project name is checked by ota.c on every update (the image must be "ours"): it keeps its old
# name so installed watches still accept new images
project(c6-fx-demo)
