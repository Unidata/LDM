#!/bin/csh -f
set FILES=`ls *.tbl`
foreach FILE ( $FILES )
 echo FILE $FILE
 diff $FILE $GEMTBL/grid
 set STATUS=$status
 if ( $STATUS ) then
    echo non zero $STATUS
    cp $GEMTBL/grid/$FILE .
 endif
 echo '******************************'
end
