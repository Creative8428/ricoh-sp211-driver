# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# minimal one-page PDF generator with text + rectangle
import sys
def pdf(path, content):
    objs=["<< /Type /Catalog /Pages 2 0 R >>",
          "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
          "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
          "<< /Length %d >>\nstream\n%s\nendstream"%(len(content),content),
          "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    out=b"%PDF-1.4\n"; offs=[]
    for i,o in enumerate(objs):
        offs.append(len(out)); out+=("%d 0 obj\n%s\nendobj\n"%(i+1,o)).encode()
    x=len(out); out+=("xref\n0 %d\n0000000000 65535 f \n"%(len(objs)+1)).encode()
    for o in offs: out+=("%010d 00000 n \n"%o).encode()
    out+=("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n"%(len(objs)+1,x)).encode()
    open(path,"wb").write(out)
pdf("text.pdf","BT /F1 36 Tf 72 760 Td (Ricoh SP 211 test) Tj ET 50 50 495 742 re S 100 100 200 200 re f")
pdf("blank.pdf"," ")
