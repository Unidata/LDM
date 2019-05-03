/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: please see COPYRIGHT under top-level folder for details.
 *
 * @filename: BofResponseTest.cpp
 *
 * @description:
 * This file is for the test purpose against class BofResponse.
 * It targets on testing all the boundaries of input and output,
 * possible branches of the function and its robustness.
 *
 * @author: Shawn Chen <sc7cq@virginia.edu>
 * @version: 1.0
 * @date: Aug 4, 2014
 * @history:
 * 1. test file created                                 Aug  4, 2014
 * 2. test case against base class BofResponse added    Aug  8, 2014
 * 3. comments added.                                   Aug 10, 2014
 * 4. test case against class MemoryBofResponse added   Aug 10, 2014
 */


/*
 * This part of includes is fixed and provides standard CppUnit test
 * controllers and runners. Please remain unchanged.
 */
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/BriefTestProgressListener.h>
#include <cppunit/CompilerOutputter.h>
#include <cppunit/TextOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/TestResult.h>
#include <cppunit/TestRunner.h>
#include <cppunit/TestResultCollector.h>

/*
 * This part of includes is the header files of unit under test, which the
 * test class against this UUT is derived from.
 */
#include "BofResponse.h"

using namespace std;

/*
 * Always use the name of a class under test + Test as the test class's name.
 * And this class is partly fixed while we only need to modify the method
 * containing designed test cases. For each class to be tested, a simple copy
 * and paste from this template with modification upon test cases is enough.
 */
class BofResponseTest : public CppUnit::TestFixture {
    public:
        /*
         * This method will contain all the designed test cases against the
         * target class. Use CPPUNIT_ASSERT() to make a judgement if the
         * runtime result is exactly the same as it's expected to be.
         * It's also reasonable to create another test method for different
         * member functions, e.g. aTest(), bTest(). But do remember to add
         * your new method in to the CppUnit test suite below. And also update
         * printTestInfo() accordingly.
         */
        void runBofResponseTest()
        {
            BofResponse bofr_obj1(true), bofr_obj2(false);
            const BofResponse* new_bofr;

            // test on isWanted()
            CPPUNIT_ASSERT( true == bofr_obj1.isWanted() );
            CPPUNIT_ASSERT( false == bofr_obj2.isWanted() );

            // test on getIgnore()
            new_bofr = bofr_obj1.getIgnore();
            CPPUNIT_ASSERT( false == new_bofr->isWanted() );
        }

        // test class MemoryBofResponse
        void runMemoryBofResponseTest()
        {
            static char                    bofrBuf[FMTP_PACKET_LEN];
            static const MemoryBofResponse membofr_obj1(bofrBuf, sizeof(bofrBuf), true);
            static const MemoryBofResponse membofr_obj2(bofrBuf, sizeof(bofrBuf), false);
            const BofResponse* new_bofr;

            // test on isWanted()
            CPPUNIT_ASSERT( true == membofr_obj1.isWanted() );
            CPPUNIT_ASSERT( false == membofr_obj2.isWanted() );

            // test on getIgnore()
            new_bofr = membofr_obj1.getIgnore();
            CPPUNIT_ASSERT( false == new_bofr->isWanted() );

            // test on dispose()
            // remember to test dispose() using mocking lib.

            // test on getBuf()
            CPPUNIT_ASSERT( bofrBuf == membofr_obj1.getBuf() );
        }

    private:
        CPPUNIT_TEST_SUITE(BofResponseTest);
        /*
         * If there is  more than one test method in this test class. Please
         * start a new line and pass the name of new test method to
         * CPPUNIT_TEST(), e.g. CPPUNIT_TEST(a), CPPUNIT_TEST(b).
         */
        CPPUNIT_TEST(runBofResponseTest);
        CPPUNIT_TEST(runMemoryBofResponseTest);
        CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION(BofResponseTest);


/*
 * @function name: printTestInfo
 * @description:
 * This is a function printing some test info to the stdout. A suggested way
 * is to write some output messages about what method has been tested by test
 * cases.
 * @input: None
 * @output: None
 * @return: None
 */
void printTestInfo()
{
    cout << endl << "Testing list:" << endl;
    cout << "\tBofResponse::isWanted() \tpassed." << endl;
    cout << "\tBofResponse::getIgnore() \tpassed." << endl;
    cout << "\tMemoryBofResponse::isWanted() \tpassed." << endl;
    cout << "\tMemoryBofResponse::dispose() \tuntested." << endl;
    cout << "\tMemoryBofResponse::getIgnore() \tpassed." << endl;
    cout << "\tMemoryBofResponse::getBuf() \tpassed." << endl;
}


/*
 * Do not change function main(). This is a fixed universal body of testing
 * procedure. It can be adapted to all test cases running by CppUnit. So just
 * re-write test cases for each class and remain the interfaces unchanged.
 */
int main(int argc, char* argv[])
{
    // Create the event manager and test controller
    CppUnit::TestResult controller;

    // Add a listener that colllects test result
    CppUnit::TestResultCollector result;
    controller.addListener(&result);

    // Add a listener that print dots when test running.
    CppUnit::BriefTestProgressListener progress;
    controller.addListener(&progress);

    CppUnit::TextUi::TestRunner runner;
    CppUnit::TestFactoryRegistry &registry = CppUnit::TestFactoryRegistry::getRegistry();
    runner.addTest( registry.makeTest() );
    cout << endl << "Running Tests " << endl;
    runner.run(controller);

    // Print test in a compiler compatible format.
    cout << endl << "Test Results" << endl;
    CppUnit::CompilerOutputter outputter( &result, CppUnit::stdCOut() );
    outputter.write();

    // print info of passed test cases here
    printTestInfo();

    return result.wasSuccessful() ? 0 : 1;
}
