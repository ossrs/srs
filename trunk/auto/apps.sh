# generate the binary
#
# params:
#     $SRS_OBJS the objs directory. ie. objs
#     $SRS_MAKEFILE the makefile name. ie. Makefile
#
#     $MAIN_ENTRANCES array, disable all except the $APP_MAIN itself. ie. ["srs_main_server" "srs_main_bandcheck"]
#     $APP_MAIN the object file that contains main function. ie. srs_main_server
#     $BUILD_KEY a string indicates the build key for Makefile. ie. srs
#     $APP_NAME the app name to output. ie. srs
#     $MODULE_OBJS array, the objects to compile the app.
#     $ModuleLibFiles array, the 3rdpart library file to link with. ie. [objs/st-1.9/obj/libst.a objs/libx264/obj/libx264.a]
#     $LINK_OPTIONS the linker options. ie. -ldl

FILE=${SRS_OBJS}/${SRS_MAKEFILE}

APP_TARGET="${SRS_OBJS}/${APP_NAME}"

echo "generate app ${APP_NAME} depends...";

echo "# build ${APP_TARGET}" >> ${FILE}
# generate the binary depends, for example:
#       srs: objs/srs
echo "${BUILD_KEY}: ${APP_TARGET}" >> ${FILE}
# the link commands, for example:
#       objs/srs: objs/src/core/srs_core.o
echo -n "${APP_TARGET}: " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    ignored=0
    for disabled_item in ${MAIN_ENTRANCES[*]}; do
        if [[ ${FILE_NAME} == ${disabled_item} && ${FILE_NAME} != ${APP_MAIN} ]]; then
            ignored=1
            continue;
        fi
    done
    
    if [ ! -f ${item} ]; then
        ignored=1
    fi
    
    if [ ${ignored} == 1 ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo "" >> ${FILE}

echo "generate app ${APP_NAME} link...";

# genereate the actual link command, for example:
#       	$(LINK)  -o objs/srs objs/src/core/srs_core.o -ldl
echo -n "	\$(LINK) -o ${APP_TARGET} " >> ${FILE}
for item in ${MODULE_OBJS[*]}; do
    FILE_NAME=`basename $item`
    FILE_NAME=${FILE_NAME%.*}
    
    ignored=0
    for disabled_item in ${MAIN_ENTRANCES[*]}; do
        if [[ ${FILE_NAME} == ${disabled_item} && ${FILE_NAME} != ${APP_MAIN} ]]; then
            ignored=1
            continue;
        fi
    done
    
    if [ ! -f ${item} ]; then
        ignored=1
    fi
    
    if [ ${ignored} == 1 ]; then
        continue;
    fi
    
    OBJ_FILE=${SRS_OBJS}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
# 3rdpart library static link.
for item in ${ModuleLibFiles[*]}; do
    echo -n "$item " >> ${FILE}
done
# link options.
echo -n "${LINK_OPTIONS}" >> ${FILE}
echo "" >> ${FILE}

echo -n "generate app ${APP_NAME} ok"; echo '!';
