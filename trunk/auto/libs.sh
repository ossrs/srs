# generate the library for static link.
#
# params:
#     $SRS_OBJS the objs directory to store the Makefile. ie. ./objs
#     $SRS_OBJS_DIR the objs directory for Makefile. ie. objs
#     $SRS_MAKEFILE the makefile name. ie. Makefile
#
#     $BUILD_KEY a string indicates the build key for Makefile. ie. dump
#     $LIB_NAME the app name to output. ie. smart_server
#     $MODULE_OBJS array, the objects to compile the app.

FILE=${SRS_OBJS}/${SRS_MAKEFILE}

LIB_TARGET="${SRS_OBJS_DIR}/${LIB_NAME}"
LIB_TAGET_STATIC="${LIB_TARGET}.a"

echo "generate lib ${LIB_NAME} depends..."

echo "" >> ${FILE}
echo "# archive library ${LIB_TAGET_STATIC}" >> ${FILE}
echo "${BUILD_KEY}: ${LIB_TAGET_STATIC}" >> ${FILE}

# build depends
echo -n "${LIB_TAGET_STATIC}: " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    if [ ! -f ${item} ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS_DIR}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo "" >> ${FILE}

# build header file
echo -n "	@bash auto/generate_header.sh ${SRS_OBJS_DIR}" >> ${FILE}
echo "" >> ${FILE}

# archive librtmp.a
echo -n "	\$(AR) -rs ${LIB_TAGET_STATIC} " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    if [ ! -f ${item} ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS_DIR}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo "" >> ${FILE}

# parent Makefile, to create module output dir before compile it.
echo "	mkdir -p ${SRS_OBJS_DIR}/include" >> ${SRS_MAKEFILE}
echo "	mkdir -p ${SRS_OBJS_DIR}/lib" >> ${SRS_MAKEFILE}

echo -n "generate lib ${LIB_NAME} ok"; echo '!';
