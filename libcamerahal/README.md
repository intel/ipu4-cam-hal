#  User Guide
##  1. Build environment<br>
###  Choice 1:   
      ubuntu21.04, glibc 2.27<br>
      sudo apt-get install autoconf libtool linux-libc-dev<br>
###  Choice 2:    
      Yocto board, glibc 2.27  

##  2. Building libcamhal<br>
        mkdir build<br>
        cd build<br>
        cmake ../<br>
        make -j<br>
        make package<br>

##  3.  Install libcamhal<br>
      rpm -ivh --force --nodeps libcamhal-xxx.rpm 


