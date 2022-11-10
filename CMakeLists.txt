cmake_minimum_required(VERSION 3.23)
project(assignment3 C)

set(CMAKE_C_STANDARD 99)

add_executable(assignment3 disk_emu.h disk_emu.c sfs_api.h sfs_api.c sfs_test0.c)
