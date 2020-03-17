[![Azure CI](https://inaos.visualstudio.com/iron-array/_apis/build/status/inaos.iron-array?branchName=develop)](https://inaos.visualstudio.com/iron-array/_build/latest?definitionId=6&branchName=develop)

# iron-array

### Setup

#### Git commit-hooks

Execute the following commands:

      cp conf/pre-commit .git/hooks/


### Build

We use inac cmake build-system in combination with different libraries which can be installed using
miniconda3.  In particular, one can install MKL, IPP and SVML from Intel in a cross-platform
portable way with:

$ conda install -c intel mkl-include  # MKL
$ conda install -c intel mkl-static  # MKL
$ conda install -c intel ipp  # IPP
$ conda install -c intel icc_rt  # SVML

#### Windows

* INAC build setup
    * Make sure that you have a configured repository.txt file in ~\.inaos\cmake
    * Also you'll need a directory ~\INAOS (can be empty)

* Create a build folder

         mkdir build
         cd build

* Invoke CMAKE, we have to define the generator as well as the build-type

         cmake -G"Visual Studio 14 2015 Win64" -DCMAKE_BUILD_TYPE=Debug ..
         cmake -G"Visual Studio 14 2015 Win64" -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

#### Mac

* INAC build setup:
    * Make sure that you have a configured repository.txt file in ~/.inaos/cmake
    * Also you'll need a directory ~/INAOS (can be empty)

* Create a build folder:

         mkdir build
         cd build

* Invoke CMAKE, we have to define the build-type:

         cmake -DCMAKE_BUILD_TYPE=Debug ..
         cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

* If one wants to use the multithreaded version, then add next flag:

         cmake -DCMAKE_BUILD_TYPE=Debug -DMULTITHREADING=TRUE ..
         cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMULTITHREADING=TRUE ..
 
    
#### Linux

* INAC build setup
    * Make sure that you have a configured repository.txt file in ~/.inaos/cmake
    * Also you'll need a directory ~/INAOS (can be empty)
    
* MKL setup.  For Ubuntu machines, it is best to use Intel's Ubuntu repo (but you can use conda packages described above too):

         wget https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS-2019.PUB
         apt-key add GPG-PUB-KEY-INTEL-SW-PRODUCTS-2019.PUB
         sudo sh -c 'echo deb https://apt.repos.intel.com/mkl all main > /etc/apt/sources.list.d/intel-mkl.list'
         sudo apt-get update && sudo apt-get install intel-mkl-64bit-2019.X

* Create a build folder

         mkdir build
         cd build

* Invoke CMAKE, we have to define the build-type, but only two types are supported

         cmake -DCMAKE_BUILD_TYPE=Debug ..
         cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

* In some Linux, the way to detect LLVM is different, so for example for Clear Linux, one must use:

         cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLEARLINUX=TRUE ..
         
* If one wants to use the multithreaded version, then add next flag:

         cmake -DCMAKE_BUILD_TYPE=Debug -DMULTITHREADING=TRUE ..
         cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMULTITHREADING=TRUE ..


### Limitations

#### Expressions

* For now only element-wise operations are supported in expression.

