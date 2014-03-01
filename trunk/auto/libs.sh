# generate the library for static link.
#
# params:
#     $SRS_OBJS the objs directory. ie. objs
#     $SRS_MAKEFILE the makefile name. ie. Makefile
#
#     $BUILD_KEY a string indicates the build key for Makefile. ie. dump
#     $LIB_NAME the app name to output. ie. smart_server
#     $MODULE_OBJS array, the objects to compile the app.
#     $LINK_OPTIONS the linker options to generate the so(shared library).

FILE=${SRS_OBJS}/${SRS_MAKEFILE}

LIB_TARGET="${SRS_OBJS}/${LIB_NAME}"
LIB_TAGET_STATIC="${LIB_TARGET}.a"
LIB_TAGET_SHARED="${LIB_TARGET}.so"

echo "generate lib ${LIB_NAME} depends..."

echo "" >> ${FILE}
echo "# archive library ${LIB_TAGET_STATIC}" >> ${FILE}
echo "${BUILD_KEY}: ${LIB_TAGET_SHARED}" >> ${FILE}

# build depends
echo -n "${LIB_TAGET_SHARED}: " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    if [ ! -f ${item} ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo "" >> ${FILE}

# build header file
echo -n "	@bash auto/generate_header.sh ${SRS_OBJS}" >> ${FILE}
echo "" >> ${FILE}

# archive librtmp.a
echo -n "	\$(AR) -rs ${LIB_TAGET_STATIC} " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    if [ ! -f ${item} ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo "" >> ${FILE}

echo "generate lib ${LIB_NAME} link...";

# archive librtmp.so
echo -n "	\$(GCC) -shared -o ${LIB_TAGET_SHARED} " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    if [ ! -f ${item} ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo -n "${LINK_OPTIONS} " >> ${FILE}
echo "" >> ${FILE}

echo -n "generate lib ${LIB_NAME} ok"; echo '!';
