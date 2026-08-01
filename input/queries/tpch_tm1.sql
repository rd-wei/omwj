SELECT *
FROM customer, orders, lineitem
WHERE customer.C_CUSTKEY = orders.O_CUSTKEY
AND orders.O_ORDERKEY = lineitem.L_ORDERKEY;