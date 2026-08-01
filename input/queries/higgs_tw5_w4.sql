SELECT *
FROM follows1, users1, users2, users3, follows2
WHERE follows1.F1_FOLLOWED = users1.U1_USERKEY
AND users1.U1_JOINDATE > users2.U2_JOINDATE - 4
AND users1.U1_JOINDATE < users2.U2_JOINDATE
AND users2.U2_JOINDATE > users3.U3_JOINDATE - 4
AND users2.U2_JOINDATE < users3.U3_JOINDATE
AND follows2.F2_FOLLOWER = users3.U3_USERKEY;
