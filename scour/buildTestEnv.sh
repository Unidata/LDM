#!/bin/bash

mkdir -p  ~/dev/testTree/titi
mkdir -p  ~/dev/testTree/toto


touch  ~/dev/testTree/titi/aFileToo.foo
touch  ~/dev/testTree/titi/aFile.txt
touch  ~/dev/testTree/titi/.scour
touch  ~/dev/testTree/titi/.scour$*.foo
ln -s  ~/dev/testTree/toto/tata.txt  ~/dev/testTree/titi/sl_tata_file 
ln -s  ~/dev/testTree/toto/tut_dir ~/dev/testTree/titi/sl_toto_tut_dir 

ls -la ~/dev/testTree/titi/
tree ~/dev/testTree/titi/

touch  ~/dev/testTree/toto/tata.txt
touch  ~/dev/testTree/toto/titi.foo
touch  ~/dev/testTree/toto/.scour
touch  ~/dev/testTree/toto/.scour$*.foo
mkdir -p  ~/dev/testTree/toto/tut_dir

ls -la ~/dev/testTree/toto/
tree ~/dev/testTree/toto/ 
