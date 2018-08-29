Folder structure:
    vcmtp--protocol--*.cpp/*.h
           |
           |
           test------protocol----BofResponse---*.cpp/*.h/Makefile
                                 |
                                 |
                                 ...

The test folder contains all the test code. Inside there is a protocol folder
which is corresponding to the protocol folder on the same level with test
folder. Within this test/protocol folder, there is a BofResponse folder. It's
where all test codes regarding this BofResponse class are stored. In the future
there should be more folders named after each class. As an example, you can
find 2 .cpp file, 1 .h file and 1 .sh file inside test/protocol/BofResponse.
The BofResponse.cpp and BofResponse.h are original (actually not, with a little
modification) source codes and BofResponseTest.cpp is the test code targeting
those two files. The runcompile.sh is a bash shell script written to do the
compilation automatically.
To begin with the unit test, just type following commands in a terminal under
Linux/Unix. Ubuntu 14.04 LTS is recommended.
$ sudo apt-get install libcppunit-dev
$ cd $(YOUR_PATH_TO)/vcmtp/test/protocol/BofResponse
$ make
$ ls
BofResponse.cpp    BofResponse.h    BofResponseTest.cpp    UnitTest
$ ./UnitTest
Running Tests
BofResponseTest::runBofResponseTest : OK
BofResponseTest::runMemoryBofResponseTest : OK

Test Results
OK (2)

Testing list:
    BofResponse::isWanted()         passed.
    BofResponse::getIgnore()        passed.
    MemoryBofResponse::isWanted()   passed.
    MemoryBofResponse::dispose()    untested.
    MemoryBofResponse::getIgnore()  passed.
    MemoryBofResponse::getBuf()     passed.

The printout messages show that all of the test cases except dispose() have
been passed. From the class perspective, BofResponse and MemoryBofResponse
are tested fine.

TODO:
1. Apply mocking libs into CppUnit to test those abstract classes.
2. Complete the remaining unit tests against other classes.
3. Adjust folder structure and include paths
