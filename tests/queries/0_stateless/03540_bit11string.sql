-- Basic encoding tests
SELECT encodeBit11String(11, 0) = 'aa';
SELECT encodeBit11String(5, 7, 6, 3) = 'hr';
SELECT encodeBit11String(11, 2047) = '7b';
SELECT encodeBit11String(11, 1, 11, 1) = 'abab';
SELECT encodeBit11String(22, 4194303) = '7b7b';

-- Basic decoding tests
SELECT decodeBit11String(arrayJoin(['aa']), 11) = [0];
SELECT decodeBit11String(arrayJoin(['hr']), 5, 6) = [7, 3];
SELECT decodeBit11String(arrayJoin(['7b']), 11) = [2047];

-- Round-trip tests
SELECT decodeBit11String(encodeBit11String(11, 100), 11) = [100];
SELECT decodeBit11String(encodeBit11String(5, 7, 6, 3), 5, 6) = [7, 3];
SELECT decodeBit11String(encodeBit11String(11, 100, 11, 200), 11, 11) = [100, 200];

-- NULL handling
SELECT encodeBit11String(11, NULL) IS NULL;
SELECT decodeBit11String(arrayJoin([NULL]), 11) IS NULL;

-- Error cases (should fail)
-- SELECT encodeBit11String(5, 0, 5, 0); -- { serverError BAD_ARGUMENTS }  -- Total bits not multiple of 11
-- SELECT encodeBit11String(3, 8); -- { serverError BAD_ARGUMENTS }  -- Value exceeds bit length
-- SELECT decodeBit11String(arrayJoin(['a!']), 11); -- { serverError BAD_ARGUMENTS }  -- Invalid character
-- SELECT decodeBit11String(arrayJoin(['aaa']), 11); -- { serverError BAD_ARGUMENTS }  -- Odd length string