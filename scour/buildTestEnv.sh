#!/bin/bash

# Directory         Days-old    Optional-filename-pattern

#dir0
#~/toto                 7-1130  *.txt   
##~miles                    2       *.foo  	------------
#~/toto         			2       *.foo   
#~/titi         			2-0732  *.foo   
#~/toto         			3-09    *.foo   
##~/titi          			2       *.txt  	------------
#~miles/toto            	2       *.foo   
##dir3                    	4       *.boo	------------
##~ldm/var/logs           	1       *.stats	------------
##dir4                    	4				------------
##dir5                    	9				------------




function modifyFile_in_test_symlinkDeletion()
{
	time_now=`printf  '%(%Y%m%d%H%M)T\n' -1`

	timestamp_1="202101281151"
	timestamp_2="202101281152"
	timestamp_3="202101281153"
	timestamp_4="202101281154"
	timestamp_5="202101281155"
	timestamp_6="202101281156"

	declare -A fileToTimestampArray=(
		[~/titi/.scour$*.foo]="$time_now"
		[~/titi/aFileToo.foo]="$timestamp_2"
		[~/titi/aFile.txt]="$timestamp_3"
		[~/toto/tata.txt]="$timestamp_4"
		[~/toto/titi.foo]="$timestamp_5"
		[~/toto/.scour$*.foo]="$timestamp_6"
	)

	# Create files
	echo -e "\nCreating files: "
	for file in "${!fileToTimestampArray[@]}"; do
		echo -e "\t - $file \twith mtime ${fileToTimestampArray[$file]}"
		touch -m -t "${fileToTimestampArray[$file]}"  "$file"
	done

}


function createSymlinks()
{
	# declare -A fileToSymlinkArray=(
		
	# 	[~/toto/tata.txt]="~/titi/sl_toto_file"
	# 	[~/toto/tut_dir]="~/titi/sl_toto_tut_dir"
	# )
	local decalre -A fileToSymlinkArray="$@"
	# Create symlinks
	echo -e "\nCreating symlink(s):"
	#for file in "${!fileToSymlinkArray[@]}"; do
	for file in $@; do
		echo -e "\t - ${fileToSymlinkArray[$file]} ---> $file"
		ln -s "$file" "${fileToSymlinkArray[$file]}" > /dev/null 2>&1
	done
}

function test_symlinkDeletion()
{
	# Setup
	cleanUp

	modifyFile_in_test_symlinkDeletion

	declare -A fileToSymlinkArray=(
		
		[~/toto/tata.txt]="~/titi/sl_toto_file"
		[~/toto/tut_dir]="~/titi/sl_toto_tut_dir"
	)

	createSymlinks "${fileToSymlinkArray[@]}"

	# run Cscour
	Cscour_cmd="./Cscour -v -d  /tmp/scour_ingest"
	echo -e "\n\t$Cscour_cmd\n"
#	$Cscour_cmd

	# assertions
	assertChange_in_test_symlinkDeletion
}


function assertChange_in_test_symlinkDeletion()
{
	# Still there
	assertDirsExist
	assertFilesExist

	# Those removed
	assertDirsDeleted
	assertFilesDeleted
	
	declare -A fileToSymlinkArray=(
	
		[~/toto/tata.txt]="~/titi/sl_toto_file"
		[~/toto/tut_dir]="~/titi/sl_toto_tut_dir"
	)

	assertSymlinksDeleted  ${fileToSymlinkArray[@]}

}

function assertSymlinksDeleted()
{
	echo -e "\nDeleting symlinks...\n"
	#arrayParam=("${!1}")
	arrayParam=$1
	echo -e "---------------------"
	echo "${arrayParam[@]}"
	echo -e "---------------------"

	#for file in "${arrayParam[@]}"; do
	for file in "${arrayParam[@]}"; do
		#syml=${arrayParam[$file]}
		echo "$file"
#		[ ! -f "$file" ] && echo -e "\tSymbolic link $symlink does NOT exist. \tEXPECTED! "
	
	done

	return 0;
}

function assertDirsExist()
{
	declare dirArray=(
			"~/titi"
			"~/toto"
			"~/toto/tut_dir"
	)

	for dir in "${dirArray[@]}"; do
		[ -d "$dir" ] && echo  -e "\tDirectory $dir exists. \tEXPECTED."
		[ ! -d "$dir" ] && echo -e "\tDirectory $dir exists. \tUNEXPECTED! Test failed!"
	done

	return 0;
}

function assertDirsDeleted()
{
	declare dirArray=(
			"~/toto/tut_dir"
	)

	for dir in "${dirArray[@]}"; do
		[ ! -d "$dir" ] && echo -e "\tDirectory $dir does NOT exist. \tEXPECTED! "
	done

	return 0;
}

function assertFilesExist()
{
	return 0;
}

function assertFilesDeleted()
{	
	return 0;
}

function cleanUp()
{
	local dirArray="$1"
	# Create directories
	echo -e "\nCreating directories:"
	for dir in "${dirArray[@]}"; do
		echo -e "\t - $dir"
		rm -rf $dir
		mkdir -p "$dir"
	done
}

#-------------------------------------- main() --------------------------------

	declare dirArray=(
			"~/titi"
			"~/toto"
			"~/toto/tut_dir"
	)
	cleanUp "$dirArray[@]"

	tree ~/titi
	exit
	# Scenario 1:
	test_symlinkDeletion

	#scenario_2
