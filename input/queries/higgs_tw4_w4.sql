SELECT *
FROM follows1, users1, users2, follows2
WHERE follows1.F1_FOLLOWED = users1.U1_USERKEY
AND users1.U1_JOINDATE > users2.U2_JOINDATE - 4
AND users1.U1_JOINDATE < users2.U2_JOINDATE
AND follows2.F2_FOLLOWER = users2.U2_USERKEY;
