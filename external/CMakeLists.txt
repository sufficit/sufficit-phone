
add_subdirectory(linphone-sdk/)
# QtKeychain is added unconditionally from the root CMakeLists.txt: it is
# needed in every configure mode, including LINPHONE_QT_ONLY, because the
# persistent session depends on it.
add_subdirectory(google)