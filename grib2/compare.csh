#!/bin/csh -f

set FILES=`ls *.[ch]`

foreach FILE ($FILES)

   echo FILE $FILE
   diff $FILE ../g2clib-1.0.2
end
