CC=clang
CFLAGS=
LIBS=-lopenblas -lpthread

PROGRAMA = cello.pgl
CFILE = extr_matmul_c.c_mm_mul.c
MLIRFILE = modified.mlir
LLFILE = modified.ll
PATFILE = pat.pat

$(PROGRAMA): $(LLFILE)
	$(CC) $(LLFILE) $(CFLAGS) $(LIBS) -o $(PROGRAMA)

$(LLFILE): $(MLIRFILE)
	cir-translate --cir-to-llvmir $(MLIRFILE) | sed '/^target datalayout/d' > $(LLFILE)

$(MLIRFILE): $(CFILE) $(PATFILE)
	smr $(CFILE) $(PATFILE) -o $(MLIRFILE)

clean:
	rm -f $(PROGRAMA) $(MLIRFILE) $(LLFILE)
