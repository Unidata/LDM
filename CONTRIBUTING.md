# How to Contribute to the LDM Package

## Adding a Feature and Making a Pull Request

1. Clone the repository:

        git clone --recursive https://github.com/Unidata/LDM.git src
    
2. Change to the source-directory:

        cd src

3. Create the autotools(1) infrastructure:

        mkdir -p m4
        autoreconf -f -i

4. Create the build infrastructure:

        ./configure --enable-debug --disable-root-actions ... |& tee configure.log

5. Edit the package to add your feature. Start by adding a test of the feature to `make check`. Feature additions lacking this verification will not be accepted.

6. Build the package:

        make >&all.log

7. Test the package:

        make check >&check.log && echo Checked && make distcheck >&distcheck.log && echo Distchecked

8. Repeat steps 5 through 7 until 7 succeeds.

9. Merge any changes in the upstream branch. Pull requests that have not done this will not be accepted.

        git pull

10. Commit your changes:

        git commit -a

11. Make a pull request
