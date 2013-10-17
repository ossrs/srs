#!/bin/bash

# params:
# $GLOBAL_DIR_OBJS the objs directory. ie. objs
# $GLOBAL_FILE_MAKEFILE the makefile name. ie. Makefile
# $MAIN_ENTRANCES array, all main entrance, disable all except the $APP_MAIN itself
# $MODULE_OBJS array, the objects to compile the app.
# $BUILD_KEY a string indicates the build key for Makefile. ie. dump
# $APP_MAIN the object file that contains main function. ie. your_app_main
# $APP_NAME the app name to output. ie. your_app
# $ModuleLibFiles array, the 3rdpart library file to link with. ie. (objs/st-1.9/obj/libst.a objs/libx264/obj/libx264.a)
# $LINK_OPTIONS the linker options.
# $SO_PATH the libssl.so.10 and other so file path.

FILE=${GLOBAL_DIR_OBJS}/${GLOBAL_FILE_MAKEFILE}

APP_TARGET="${GLOBAL_DIR_OBJS}/${APP_NAME}"

echo "generate app ${APP_NAME} depends...";

echo "# build ${APP_TARGET}" >> ${FILE}
echo "${BUILD_KEY}: ${APP_TARGET}" >> ${FILE}

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
    
    OBJ_FILE=${GLOBAL_DIR_OBJS}/$item
    OBJ_FILE="${OBJ_FILE%.*}.o"
    echo -n "${OBJ_FILE} " >> ${FILE}
done
echo "" >> ${FILE}

echo "generate app ${APP_NAME} link...";

echo -n "	\$(LINK) ${PerformanceLink} -o ${APP_TARGET} " >> ${FILE}
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
    
    OBJ_FILE=${GLOBAL_DIR_OBJS}/$item
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

# set the so reference path.
if [[ ! -z ${SO_PATH} ]]; then
    echo -n "	@bash auto/set_so_rpath.sh ${SOPathTool} ${APP_TARGET} ${SO_PATH}" >> ${FILE}
    echo "" >> ${FILE}
fi

echo -n "generate app ${APP_NAME} ok"; echo '!';
