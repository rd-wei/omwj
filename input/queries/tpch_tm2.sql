SELECT *
FROM supplier, customer, nation1, nation2
WHERE supplier.S_NATIONKEY = nation1.N1_N_NATIONKEY
AND customer.C_NATIONKEY = nation2.N2_N_NATIONKEY
AND nation1.N1_N_REGIONKEY = nation2.N2_N_REGIONKEY;