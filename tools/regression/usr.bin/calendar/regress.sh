# $FreeBSD$

CALENDAR_FILE="-f calendar.calibrate"
CALENDAR_BIN="calendar"

CALENDAR="${CALENDAR_BIN} ${CALENDAR_FILE}"

REGRESSION_START($1)

echo 1..4

REGRESSION_TEST(`s1',`$CALENDAR -t 29.12.2006')
REGRESSION_TEST(`s2',`$CALENDAR -t 30.12.2006')
REGRESSION_TEST(`s3',`$CALENDAR -t 31.12.2006')
REGRESSION_TEST(`s4',`$CALENDAR -t 01.01.2007')

echo 5..9

REGRESSION_TEST(`a1',`$CALENDAR -A 3 -t 28.12.2006')
REGRESSION_TEST(`a2',`$CALENDAR -A 3 -t 29.12.2006')
REGRESSION_TEST(`a3',`$CALENDAR -A 3 -t 30.12.2006')
REGRESSION_TEST(`a4',`$CALENDAR -A 3 -t 31.12.2006')
REGRESSION_TEST(`a5',`$CALENDAR -A 3 -t 01.01.2007')

echo 10..14

REGRESSION_TEST(`b1',`$CALENDAR -B 3 -t 31.12.2006')
REGRESSION_TEST(`b2',`$CALENDAR -B 3 -t 01.01.2007')
REGRESSION_TEST(`b3',`$CALENDAR -B 3 -t 02.01.2007')
REGRESSION_TEST(`b4',`$CALENDAR -B 3 -t 03.01.2007')
REGRESSION_TEST(`b5',`$CALENDAR -B 3 -t 04.01.2007')

REGRESSION_END()
