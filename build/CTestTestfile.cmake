# CMake generated Testfile for 
# Source directory: /home/bismarck/FastDevFs
# Build directory: /home/bismarck/FastDevFs/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ADTTests "/home/bismarck/FastDevFs/build/test_adt")
set_tests_properties(ADTTests PROPERTIES  _BACKTRACE_TRIPLES "/home/bismarck/FastDevFs/CMakeLists.txt;65;add_test;/home/bismarck/FastDevFs/CMakeLists.txt;0;")
add_test(HashTests "/home/bismarck/FastDevFs/build/test_hash")
set_tests_properties(HashTests PROPERTIES  _BACKTRACE_TRIPLES "/home/bismarck/FastDevFs/CMakeLists.txt;66;add_test;/home/bismarck/FastDevFs/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
