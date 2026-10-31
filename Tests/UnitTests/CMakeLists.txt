#======================== Unit tests configuration ============================#
# Unit tests of DbgMcu module. Registered in module CMakeLists.txt by
# UnitTesting_AddPath() and created by UnitTesting_Generate() in "UnitTest"
# build (see EmBi_Platform/CMake/UnitTesting/README.md).

get_filename_component(DBGMCU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The module uses no other module (the DBGMCU has no clock enable and no interrupt),
# no mock is needed. Tests of the read-back verification use the HW model of RegMem
# (register value which does not stay as written) - executed serially where the model
# runs in a thread. DBGMCU (0x44024000) is inside of the peripheral region of RegMem.
UnitTesting_Add_Test(
    NAME            DbgMcu
    TEST_SOURCE     Test_DbgMcu.c
    UUT_SOURCES     ${DBGMCU_DIR}/DbgMcu.c
                    ${DBGMCU_DIR}/DbgMcu_Reg.c
    INCLUDE_DIRS    ${DBGMCU_DIR}
    LINK_LIBS       ut_RegMem                           # Registers in host memory
                    ut_CmsisHost                        # CMSIS core intrinsics on host
    FORCE_INCLUDES  ${UNIT_TESTING_CMSIS_HOST_HEADER}   # Replaces cmsis_gcc.h
    HOST_LIBS       Ral_Lib                             # ST LL compiled for host
    COVERAGE_EXCLUDE ".*/Ral/.*"
    RUN_SERIAL
)
