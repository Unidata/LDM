#!/bin/bash

function cleanUp()
{
	scriptDir="$1"
	CDir="$2"
	tlm_S_outputFile="$3"
	tlm_C_outputFile="$4"
	
	rm -f "$tlm_S_outputFile"
	rm -f "$tlm_C_outputFile"

	rm -rf "$scriptDir"
	rm -rf "$CDir"

	mkdir "$scriptDir"
	mkdir "$CDir"
	
}

function duplicateTlm()
{

	scriptDir=$1 
	CDir=$2
	if [ ! -d "$scriptDir" ] || [  -z "$(ls -A $scriptDir)"  ]; then
		echo -e "\n\tDirectory does not exist or is empty! Please run \"aSpringTree\" first.\n\n"
		exit -1
	fi
	
	cd "$scriptDir"
	pwd
	ls
	cp -a . "$CDir/"
	cd "$CDir"
	pwd
	ls
}

function diffThem()
{
	scriptDir="$1"
	CDir="$2"
	
	
	tlm_S_outputFile="$3"
	tlm_C_outputFile="$4"

	cd "$scriptDir"
	find . > "$tlm_S_outputFile" 

	cd "$CDir"
	find . > "$tlm_C_outputFile" 

	echo -e "\n\tdiff $tlm_S_outputFile" "$tlm_C_outputFile:"
	echo -e "\tdiff - BEGIN\n"
	diff "$tlm_S_outputFile" "$tlm_C_outputFile"
	echo -e "\n\tdiff END\n"

	cd "$PWD"
}

#-------------------------------------- main() --------------------------------
	clear
	PWD=`pwd`

	# find . output files
	tlm_S_output="$PWD/tlm_S_output.txt"
	tlm_C_output="$PWD/tlm_C_output.txt"


	# tree  2 roots: script and Cscour
	tlm_S_data_dir="$PWD/tlm_S_data"
	tlm_C_data_dir="$PWD/tlm_C_data"

	echo -e "\n\n\t0. Tearing down previous Tree..."
	cleanUp "$tlm_S_data_dir"   "$tlm_C_data_dir"  "$tlm_S_output"   "$tlm_C_output"  

	aSpringTree="$HOME/aSpringTree -i 3 $tlm_S_data_dir "
	clear
	echo -e "\n\t1. Building a Spring Tree (many branches and a bunch of leaves). This may take a few seconds..."
	echo -e "\n\t--> Running $aSpringTree from $HOME."
	if [ -f "$HOME/aSpringTree" ]; then 
		$aSpringTree > "$PWD"/scour_log.txt
	else
		echo -e "Could not find $aSpringTree program. Bailing out!..."
		exit -1
	fi

	clear
	# Duplicate tree before running the scour script:
	echo -e "\n\n\t2. Duplicating the Tree..."
	duplicateTlm "$tlm_S_data_dir"   "$tlm_C_data_dir" 

	# 1. Run the scour.sh script on tlm_script_data_dir
	scour_S_ingest="/tmp/scour_S_ingest.conf"
	scour_C_ingest="/tmp/scour_C_ingest.conf"
	echo -e "\n\n\n\t3. Launching the scour script with $scour_S_ingest on $tlm_S_data_dir\n"
	scourScript_sh="$HOME/projects/ldm/bin/scour $scour_S_ingest"
	echo -e "\t--> Running $scourScript_sh"
	time $scourScript_sh

	# 2. Run the Cscour.c on tlm_C_data_dir
	echo -e "\n\n\t4. Launching Cscour on $tlm_C_data_dir\n"
	Cscour="$HOME/dev/scour/filesToGit/Cscour -d $scour_C_ingest"
	echo -e "\t--> Running $Cscour"
	time $Cscour

	# 3. Check the diff in /tmp
	echo -e "n\n\t5. diff-ing the 2 output files...into file: /tmp/scour_log.txt \n"
	diffThem "$tlm_S_data_dir" "$tlm_C_data_dir" "$tlm_S_output" "$tlm_C_output"  > /tmp/scour_log.txt

