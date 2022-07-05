
# The main dir of st.
ST_DIR = ..
# The main dir of st utest.
ST_UTEST = .
# The main dir of gtest.
GTEST_DIR = $(ST_UTEST)/gtest

# Flags passed to the C++ compiler.
CXXFLAGS +=  -g -O0 -std=c++11
CXXFLAGS += -DGTEST_USE_OWN_TR1_TUPLE=1
# Flags for warnings.
WARNFLAGS += -Wall -Wno-deprecated-declarations -Wno-unused-private-field -Wno-unused-command-line-argument

# House-keeping build targets.
all : $(ST_DIR)/obj/st_utest

clean :
	rm -f $(ST_DIR)/obj/st_utest* $(ST_DIR)/obj/gtest*

# Usually you shouldn't tweak such internal variables, indicated by a
# trailing _.
GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_DIR)/include/gtest/*.h $(GTEST_DIR)/include/gtest/internal/*.h

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
$(ST_DIR)/obj/gtest-all.o : $(GTEST_SRCS_)
	$(CXX) -c $(GTEST_DIR)/src/gtest-all.cc -o $@ \
        $(CXXFLAGS) $(UTEST_FLAGS) \
        $(WARNFLAGS) \
        -I$(GTEST_DIR)/include -I$(GTEST_DIR)

$(ST_DIR)/obj/gtest.a : $(ST_DIR)/obj/gtest-all.o
	$(AR) $(ARFLAGS) $@ $^

#####################################################################################
#####################################################################################
# ST(state-threads) utest section
#####################################################################################
#####################################################################################

# Depends, the depends objects
ST_UTEST_DEPS = $(ST_DIR)/obj/libst.a

# Depends, utest header files
UTEST_DEPS = $(ST_UTEST)/st_utest.hpp

# Objects, build each object of utest
$(ST_DIR)/obj/st_utest.o : st_utest.cpp $(ST_UTEST_DEPS) $(UTEST_DEPS)
	$(CXX) -c st_utest.cpp -o $@ \
        $(CXXFLAGS) $(UTEST_FLAGS) \
        $(WARNFLAGS) \
        -I$(GTEST_DIR)/include -I$(ST_UTEST) -I$(ST_DIR) -I$(ST_DIR)/obj

# generate the utest binary
$(ST_DIR)/obj/st_utest : $(ST_DIR)/obj/st_utest.o $(ST_DIR)/obj/gtest.a $(ST_UTEST_DEPS)
	$(CXX) -o $@ $(CXXFLAGS) $(UTEST_FLAGS) \
        -lpthread -ldl $^
