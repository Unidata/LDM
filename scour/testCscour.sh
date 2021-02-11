/**
 * This file declares the API for mapping from unit systems to their associated
 * pointers for version 2 of the Unidata UDUNITS package.
 *
 *  @file:  testCscour.sh
 * @author: Mustapha Iles
 *
 *    Copyright 2021 University Corporation for Atmospheric Research
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#!/bin/bash

function modifyThisList()
{
	TST_DIR=$1
	TOUCH_FILE_DIR_LIST="$3"

	# Modify the file's creation/access time to that of Jan 1, 2021, at 00:30
	# touch -a -m -t 201512180130.09 fileName.ext
	# -a = accessed
	# -m = modified
	# -t = timestamp - use [[CC]YY]MMDDhhmm[.ss] time format

	OLDER_TIME="$2"
	for path in ${TOUCH_FILE_DIR_LIST[@]}; do
		echo -e "\t $path"
		touch -a -m -t $OLDER_TIME $path
		#ls -la $path
	done
}

function createTestDir()
{
	MODF_TREE=$1
	REF_TREE=$2
	rm -rf $MODF_TREE  &>/dev/null
	mkdir -p $MODF_TREE  &>/dev/null
	
	# Create test directory tree
	cp -r $REF_TREE/* $MODF_TREE  &>/dev/null
}
																															
function assertDeleted()
{
	CHECK_FILE_DIR_LIST="$1"	
	for path in ${CHECK_FILE_DIR_LIST[@]}; do
		[ ! -e $path ] && echo -e "\t$path REMOVED!" || echo -e "\n\t$path NOT removed!"
	done
}


function assertNotDeleted()
{
	CHECK_FILE_DIR_LIST="$1"
	TOP_TREE="$2"
	for dir in $TOP_TREE ; do
		for path in ${CHECK_FILE_DIR_LIST[@]}; do
			[ $dir == $path ] && echo "$path was NOT deleted (more recent than 'Old Days' )"
		done

	done
	echo -e "Done."
}

#----------------------------- main -------------------

	SCOUR_DIR=$HOME/dev/scour
	SCOUR_PROGRAM=$SCOUR_DIR/scour
	MODIFIED_TREE=$SCOUR_DIR/modifiedTestTree
	REFERENCE_TREE=$SCOUR_DIR/referenceTestTree

	PAST_TIME="202101010030"

	declare -a listOfFiles=(
		"$MODIFIED_TREE/d1/toto3.txt" 
		"$MODIFIED_TREE/d2/d7/toto8.txt" 
		"$MODIFIED_TREE/toto2.txt" 
		"$MODIFIED_TREE/d1/toto3.txt"
		"$MODIFIED_TREE/d2/d6/d10/toto10.txt"
	)
	TARGETED_FILES="${listOfFiles[*]}"
	
	clear
	echo -e "\n\n\t== Starting...\n"
	
	createTestDir $MODIFIED_TREE $REFERENCE_TREE

	echo -e "\n\n\t== Targeted files...\n"
	modifyThisList $MODIFIED_TREE $PAST_TIME "$TARGETED_FILES"

	echo -e "\n\t== Directory tree before Calling C_scour...\n"
	tree $MODIFIED_TREE

	echo -e "\n\t== Calling C_scour...\n"
	$SCOUR_PROGRAM  2>/dev/null
	
	echo -e "\n\t== Directory tree after Calling C_scour...\n"
	tree $MODIFIED_TREE

	echo -e "\n\t== Assert that all targeted files were indeed deleted...\n"
	assertDeleted "$TARGETED_FILES"

	echo -e "\n\t== Assert that not targeted files were NOT deleted...\n"
	assertNotDeleted "$TARGETED_FILES" "$MODIFIED_TREE"

	echo -e "\n\tDone.\n"