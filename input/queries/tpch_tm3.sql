SELECT *
FROM nation, supplier, customer, orders, lineitem
WHERE nation.N_NATIONKEY = supplier.S_NATIONKEY
AND supplier.S_NATIONKEY = customer.C_NATIONKEY
AND customer.C_CUSTKEY = orders.O_CUSTKEY
AND orders.O_ORDERKEY = lineitem.L_ORDERKEY;