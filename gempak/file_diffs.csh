#!/bin/csh -f
set FILES=`ls c*.c`
foreach FILE ( $FILES )
 set DIR=`echo $FILE | cut -c1-3`
 echo FILE $FILE
 diff $FILE $GEMPAK/source/cgemlib/$DIR
 set STATUS=$status
 if ( $STATUS ) then
    echo non zero $STATUS $FILE
    #cp $GEMTBL/grid/$FILE .
 endif
 echo '******************************'
end



set FILES=`ls gb2*.c`
foreach FILE ( $FILES )
 echo FILE $FILE
 diff $FILE $GEMPAK/source/gemlib/gb
 set STATUS=$status
 if ( $STATUS ) then
    echo non zero $STATUS $FILE
    #cp $GEMTBL/grid/$FILE .
 endif
 echo '******************************'
end
