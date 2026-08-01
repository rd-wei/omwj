SELECT *
FROM region, nation, supplier, partsupp, part
WHERE nation.N_REGIONKEY = region.R_REGIONKEY
AND supplier.S_NATIONKEY = nation.N_NATIONKEY
AND partsupp.PS_SUPPKEY = supplier.S_SUPPKEY
AND partsupp.PS_SUPPLYCOST > part.P_RETAILPRICE - 100
AND partsupp.PS_SUPPLYCOST < part.P_RETAILPRICE
