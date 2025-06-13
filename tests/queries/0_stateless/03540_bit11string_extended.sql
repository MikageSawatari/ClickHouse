-- Extended test cases for encodeBit11String/decodeBit11String

-- Edge cases with different bit lengths
SELECT encodeBit11String(1, 1, 10, 1023) = 'pa';  -- 1 + 1111111111 = 11111111111 = 1535
SELECT encodeBit11String(2, 3, 9, 511) = 'pa';    -- 11 + 111111111 = 11111111111 = 1535
SELECT encodeBit11String(11, 1535) = 'pa';        -- Direct encoding of 1535

-- Test all boundary values for each bit length
SELECT encodeBit11String(1, 0, 10, 0) = 'aa';     -- Minimum values
SELECT encodeBit11String(1, 1, 10, 1023) = 'pa';  -- Maximum values
SELECT encodeBit11String(2, 3, 9, 511) = 'pa';    -- Maximum values different split
SELECT encodeBit11String(3, 7, 8, 255) = 'pa';    -- Maximum values another split
SELECT encodeBit11String(4, 15, 7, 127) = 'pa';   -- Maximum values another split
SELECT encodeBit11String(5, 31, 6, 63) = 'pa';    -- Maximum values another split
SELECT encodeBit11String(6, 63, 5, 31) = 'pa';    -- Maximum values another split

-- Test with larger bit sequences (multiple 11-bit blocks)
SELECT encodeBit11String(33, 8589934591) = '7b7b7b';  -- 33-bit max value
SELECT encodeBit11String(44, 17592186044415) = '7b7b7b7b';  -- 44-bit max value
SELECT encodeBit11String(55, 36028797018963967) = '7b7b7b7b7b';  -- 55-bit max value

-- Test charset boundary characters
SELECT decodeBit11String(arrayJoin(['az']), 11) = [25];   -- 'a'=0, 'z'=25
SELECT decodeBit11String(arrayJoin(['a0']), 11) = [26];   -- 'a'=0, '0'=26
SELECT decodeBit11String(arrayJoin(['a9']), 11) = [35];   -- 'a'=0, '9'=35
SELECT decodeBit11String(arrayJoin(['aA']), 11) = [36];   -- 'a'=0, 'A'=36
SELECT decodeBit11String(arrayJoin(['aZ']), 11) = [61];   -- 'a'=0, 'Z'=61
SELECT decodeBit11String(arrayJoin(['za']), 11) = [1550]; -- 'z'=25, 'a'=0: 25*62+0
SELECT decodeBit11String(arrayJoin(['Za']), 11) = [3782]; -- 'Z'=61, 'a'=0: 61*62+0 = 3782 (exceeds 11-bit!)

-- Complex round-trip tests
SELECT decodeBit11String(encodeBit11String(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1), 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];
SELECT decodeBit11String(encodeBit11String(11, 1000, 11, 1000, 11, 1000), 11, 11, 11) = [1000, 1000, 1000];

-- Test with 64-bit values (within bit length constraints)
SELECT encodeBit11String(22, 4194303, 22, 4194303) = '7b7b7b7b';  -- Two 22-bit max values
SELECT decodeBit11String(encodeBit11String(22, 4194303, 22, 4194303), 22, 22) = [4194303, 4194303];

-- Performance test with many small values
SELECT length(encodeBit11String(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)) = 12;  -- 66 ones = 6 * 11 bits

-- NULL handling in different positions
SELECT encodeBit11String(11, 100, 11, NULL) IS NULL;
SELECT encodeBit11String(NULL, 100, 11, 200) IS NULL;
SELECT decodeBit11String(arrayJoin(['aa']), NULL) IS NULL;
SELECT decodeBit11String(arrayJoin(['aa']), 11, NULL) IS NULL;