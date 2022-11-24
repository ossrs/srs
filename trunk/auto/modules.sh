#
# Generate the module(in src, not modules) info to Makefile
#
# params:
#     $SRS_OBJS the objs directory to store the Makefile. ie. ./objs
#     $SRS_OBJS the objs directory for Makefile. ie. objs
#
#     $MODULE_DIR the module dir. ie. src/os/linux
#     $MODULE_ID the id of module. ie. CORE
#     $MODULE_DEPENDS array, the denpend MODULEs id. ie. (CORE OS)
#     $ModuleLibIncs array, the depend 3rdpart library includes. ie. (objs/st-1.9/obj objs/libx264/obj)
#     $MODULE_FILES array, the head/cpp files of modules. ie. (public log)
#     $DEFINES string, the build macro defines. ie. "-DMY_SRS"
#     
# returns:
#     $MODULE_OBJS array, the objects of the modules, used for link the binary

FILE=${SRS_OBJS}/Makefile
echo "#####################################################################################" >> ${FILE}
echo "# The module ${MODULE_ID}." >> ${FILE}
echo "#####################################################################################" >> ${FILE}
echo  >> ${FILE}

# INCS
echo "# INCS for ${MODULE_ID}, headers of module and its depends to compile" >> ${FILE}
#
# the public include files, for example:
#       CORE_MODULE_INCS = -Isrc/core
echo "${MODULE_ID}_MODULE_INCS = -I${SRS_WORKDIR}/${MODULE_DIR} " >> ${FILE}
#
# the private include files, for example:
#       CORE_INCS = -Isrc/core -Iobjs
#       CORE_LIBS_INCS = -Iobjs/st -Iobjs/ffmpeg/include
# where the public will be used for other modules which depends on it.
INCS_NAME="${MODULE_ID}_INCS"
#
# current module header files
echo -n "${INCS_NAME} = -I${SRS_WORKDIR}/${MODULE_DIR} " >> ${FILE}
#
# depends module header files
for item in ${MODULE_DEPENDS[*]}; do
    DEP_INCS_NAME="${item}_MODULE_INCS"
    echo -n "\$(${DEP_INCS_NAME})" >> ${FILE}
done
echo "" >> ${FILE}
#
# depends library header files
INCS_LIBS_NAME="${MODULE_ID}_LIBS_INCS"
echo -n "${INCS_LIBS_NAME} = " >> ${FILE}
for item in ${ModuleLibIncs[*]}; do
    echo -n "-I${item} " >> ${FILE}
done
echo "" >> ${FILE}; echo "" >> ${FILE}

# DEPS
echo "# DEPS for ${MODULE_ID}, the depends of make schema" >> ${FILE}
# depends on headers of self module, for example:
#       CORE_DEPS =  src/core/srs_core.hpp
DEPS_NAME="${MODULE_ID}_DEPS"
echo -n "${DEPS_NAME} = " >> ${FILE}
for item in ${MODULE_FILES[*]}; do
    HEADER_FILE="${SRS_WORKDIR}/${MODULE_DIR}/${item}.hpp"
    if [ -f ${HEADER_FILE} ]; then
        echo -n " ${HEADER_FILE}" >> ${FILE}
    fi
done
# depends on other modules, for example:
#       MAIN_DEPS =  $(CORE_DEPS)
for item in ${MODULE_DEPENDS[*]}; do
    DEP_DEPS_NAME="${item}_DEPS"
    echo -n " \$(${DEP_DEPS_NAME}) " >> ${FILE}
done
echo "" >> ${FILE}; echo "" >> ${FILE}

# OBJ
echo "# OBJ for ${MODULE_ID}, each object file" >> ${FILE}
MODULE_OBJS=()
for item in ${MODULE_FILES[*]}; do
    CPP_FILE="${SRS_WORKDIR}/${MODULE_DIR}/${item}.cpp"
    OBJ_FILE="${SRS_OBJS}/${MODULE_DIR}/${item}.o"
    MODULE_OBJS="${MODULE_OBJS[@]} ${MODULE_DIR}/${item}.cpp"
    if [ -f ${CPP_FILE} ]; then
        echo "${OBJ_FILE}: \$(${DEPS_NAME}) ${CPP_FILE} " >> ${FILE}
        echo "	\$(CXX) -c \$(CXXFLAGS) ${DEFINES}\\" >> ${FILE}
        echo "    \$(${INCS_NAME})\\" >> ${FILE}
        echo "    \$(${INCS_LIBS_NAME})\\" >> ${FILE}
        echo "    -o ${OBJ_FILE} \\" >> ${FILE}
        echo "    ${CPP_FILE}" >> ${FILE}
    fi
done
echo "" >> ${FILE}

# parent Makefile, to create module output dir before compile it.
echo "	@mkdir -p ${SRS_OBJS}/${MODULE_DIR}" >> ${SRS_MAKEFILE}

echo -n "Generate modules ${MODULE_ID} ok"; echo '!';
