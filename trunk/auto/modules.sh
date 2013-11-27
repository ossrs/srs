# params:
# $SRS_OBJS the objs directory. ie. objs
# $SRS_MAKEFILE the makefile name. ie. Makefile
# $MODULE_DIR the module dir. ie. src/os/linux
# $MODULE_ID the id of module. ie. CORE
# $MODULE_DEPENDS array, the denpend MODULEs id. ie. (CORE OS)
# $ModuleLibIncs array, the depend 3rdpart library includes. ie. (objs/st-1.9/obj objs/libx264/obj)
# $MODULE_FILES array, the head/cpp files of modules. ie. (public log)
# 
# returns:
# $MODULE_OBJS array, the objects of the modules.

FILE=${SRS_OBJS}/${SRS_MAKEFILE}

# INCS
INCS_NAME="${MODULE_ID}_INCS"
echo "# the ${MODULE_ID} module." >> ${FILE}
echo "${MODULE_ID}_MODULE_INCS = -I${MODULE_DIR} " >> ${FILE}
echo -n "${INCS_NAME} = -I${MODULE_DIR} " >> ${FILE}
for item in ${MODULE_DEPENDS[*]}; do
    DEP_INCS_NAME="${item}_INCS"do
    DEP_INCS_NAME="${item}_MODULE_INCS"
    echo -n "\$(${DEP_INCS_NAME}) " >> ${FILE}
done
for item in ${ModuleLibIncs[*]}; do
    echo -n "-I${item} " >> ${FILE}
done
echo "" >> ${FILE}

# DEPS
DEPS_NAME="${MODULE_ID}_DEPS"
echo -n "${DEPS_NAME} = " >> ${FILE}
for item in ${MODULE_FILES[*]}; do
    HEADER_FILE="${MODULE_DIR}/${item}.hpp"
    if [ -f ${HEADER_FILE} ]; then
        echo -n " ${HEADER_FILE}" >> ${FILE}
    fi
done
for item in ${MODULE_DEPENDS[*]}; do
    DEP_DEPS_NAME="${item}_DEPS"
    echo -n " \$(${DEP_DEPS_NAME}) " >> ${FILE}
done
echo "" >> ${FILE}; echo "" >> ${FILE}

# OBJ
MODULE_OBJS=()
for item in ${MODULE_FILES[*]}; do
    CPP_FILE="${MODULE_DIR}/${item}.cpp"
    OBJ_FILE="${SRS_OBJS}/${MODULE_DIR}/${item}.o"
    MODULE_OBJS="${MODULE_OBJS[@]} ${CPP_FILE}"
    if [ -f ${CPP_FILE} ]; then
        echo "${OBJ_FILE}: \$(${DEPS_NAME}) ${CPP_FILE} " >> ${FILE}
        echo "	\$(GCC) -c \$(CXXFLAGS) \$(${INCS_NAME})\\" >> ${FILE}
        echo "          -o ${OBJ_FILE} ${CPP_FILE}" >> ${FILE}
    fi
done
echo "" >> ${FILE}

# Makefile
echo "	mkdir -p ${SRS_OBJS}/${MODULE_DIR}" >> ${SRS_MAKEFILE}

echo -n "generate module ${MODULE_ID} ok"; echo '!';
