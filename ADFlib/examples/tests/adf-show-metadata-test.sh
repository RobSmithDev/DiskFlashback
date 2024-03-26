#!/bin/sh
basedir=`dirname "$0"`
. $basedir/common.sh

adf_show_metadata=`get_test_cmd adf_show_metadata`

$adf_show_metadata "$basedir/arccsh.adf" >$actual
compare_with <<EOF

Opening image/device:	'$basedir/arccsh.adf'
Mounted volume:		0

ADF device info:
  Type:		floppy dd, file (image)
  Geometry:
    Cylinders	80
    Heads	2
    Sectors	11

  Volumes (1):
   idx  first bl.     last bl.    name
     0          0         1759    "cshell"    mounted


ADF volume info:
  Name:		cshell                        
  Type:		Floppy Double Density, 880 KBytes
  Filesystem:	OFS  
  Free blocks:	436
  R/W:		Read only
  Created:	24/03/1999 19:35:24
  Last access:	30/07/1999 22:35:04
		30/07/1999 22:14:39

Bootblock:
  dosType:	DOS. (0x0)
  checkSum:	0xe33d0e73
  - calculated:	0xe33d0e73 -> OK
  rootBlock:	0x370 (880)

Root block sector:	880

Rootblock:
  0x000  type:		0x2		2
  0x004  headerKey:	0x0		0
  0x008  highSeq:	0x0		0
  0x00c  hashTableSize:	0x48		72
  0x010  firstData:	0x0		0
  0x014  checkSum:	0x942f7f25
     ->  calculated:	0x942f7f25 -> OK
  0x018  hashTable [ 72 ]:	(see below)
  0x138  bmFlag:	0xffffffff
  0x13c  bmPages[ 25 ]:		(see below)
  0x1a0  bmExt:		0x0
  0x1a4  cDays:		0x1ec8		7880
  0x1a8  cMins:		0x536		1334
  0x1ac  cTicks:	0x7c3		1987
  0x1b0  nameLen:	0x6		6
  0x1b1  diskName:	cshell
  0x1d0  r2[8]:			(see below)
  0x1d8  days:		0x1ec8		7880
  0x1dc  mins:		0x54b		1355
  0x1e0  ticks:		0xd7		215
  0x1e4  coDays:	0x1e48		7752
  0x1e8  coMins:	0x497		1175
  0x1ec  coTicks:	0x4da		1242
  0x1f0  nextSameHash:	0x0		0
  0x1f4  parent:	0x0		0
  0x1f8  extension:	0x371		881
  0x1fc  secType:	0x1		1

Hashtable (non-zero):
  hashtable [  8 ]:		0x372		882
  hashtable [ 17 ]:		0x3f4		1012
  hashtable [ 22 ]:		0x6d2		1746
  hashtable [ 24 ]:		0x377		887
  hashtable [ 46 ]:		0x553		1363
  hashtable [ 57 ]:		0x3f6		1014
  hashtable [ 63 ]:		0x373		883
  hashtable [ 66 ]:		0x378		888

Bitmap block pointers (bmPages) (non-zero):
  bmpages [  0 ]:		0x371		881
EOF

$adf_show_metadata "$basedir/arccsh.adf" CSH >$actual
compare_with <<EOF

Opening image/device:	'$basedir/arccsh.adf'
Mounted volume:		0

Path:		CSH

bFileHeaderBlock:
  0x000  type:		0x2		2
  0x004  headerKey:	0x3f6		1014
  0x008  highSeq:	0x48		72
  0x00c  dataSize:	0x0		0
  0x010  firstData:	0x3f7		1015
  0x014  checkSum:	0xfcb95d71
     ->  calculated:	0xfcb95d71 -> OK
  0x018  dataBlocks [ 72 ]: (see below)
  0x138  r1:		0
  0x13c  r2:		0
  0x140  access:	0x0		0
  0x144  byteSize:	0x1fc6c		130156
  0x148  commLen:	0x0		0
  0x149  comment[ 80 ]:	
  0x199  r3 [ 11 ]:		
  0x1a4  days:		6789
  0x1a8  mins:		55
  0x1ac  ticks:		200
  0x1b0  nameLen:	0x3		3
  0x1b1  fileName:	CSH
  0x1d0  r4:		0
  0x1d4  real:		0x0		0
  0x1d8  nextLink:	0x0		0
  0x1dc  r5 [ 5 ]:	(see below)
  0x1f0  nextSameHash:	0x0		0
  0x1f4  parent:	0x370		880
  0x1f8  extension:	0x43f		1087
  0x1fc  secType:	0xfffffffd	-3

  data blocks (non-zero):
    dataBlocks [  0 ]:  0x43e	1086
    dataBlocks [  1 ]:  0x43d	1085
    dataBlocks [  2 ]:  0x43c	1084
    dataBlocks [  3 ]:  0x43b	1083
    dataBlocks [  4 ]:  0x43a	1082
    dataBlocks [  5 ]:  0x439	1081
    dataBlocks [  6 ]:  0x438	1080
    dataBlocks [  7 ]:  0x437	1079
    dataBlocks [  8 ]:  0x436	1078
    dataBlocks [  9 ]:  0x435	1077
    dataBlocks [ 10 ]:  0x434	1076
    dataBlocks [ 11 ]:  0x433	1075
    dataBlocks [ 12 ]:  0x432	1074
    dataBlocks [ 13 ]:  0x431	1073
    dataBlocks [ 14 ]:  0x430	1072
    dataBlocks [ 15 ]:  0x42f	1071
    dataBlocks [ 16 ]:  0x42e	1070
    dataBlocks [ 17 ]:  0x42d	1069
    dataBlocks [ 18 ]:  0x42c	1068
    dataBlocks [ 19 ]:  0x42b	1067
    dataBlocks [ 20 ]:  0x42a	1066
    dataBlocks [ 21 ]:  0x429	1065
    dataBlocks [ 22 ]:  0x428	1064
    dataBlocks [ 23 ]:  0x427	1063
    dataBlocks [ 24 ]:  0x426	1062
    dataBlocks [ 25 ]:  0x425	1061
    dataBlocks [ 26 ]:  0x424	1060
    dataBlocks [ 27 ]:  0x423	1059
    dataBlocks [ 28 ]:  0x422	1058
    dataBlocks [ 29 ]:  0x421	1057
    dataBlocks [ 30 ]:  0x420	1056
    dataBlocks [ 31 ]:  0x41f	1055
    dataBlocks [ 32 ]:  0x41e	1054
    dataBlocks [ 33 ]:  0x41d	1053
    dataBlocks [ 34 ]:  0x41c	1052
    dataBlocks [ 35 ]:  0x41b	1051
    dataBlocks [ 36 ]:  0x41a	1050
    dataBlocks [ 37 ]:  0x419	1049
    dataBlocks [ 38 ]:  0x418	1048
    dataBlocks [ 39 ]:  0x417	1047
    dataBlocks [ 40 ]:  0x416	1046
    dataBlocks [ 41 ]:  0x415	1045
    dataBlocks [ 42 ]:  0x414	1044
    dataBlocks [ 43 ]:  0x413	1043
    dataBlocks [ 44 ]:  0x412	1042
    dataBlocks [ 45 ]:  0x411	1041
    dataBlocks [ 46 ]:  0x410	1040
    dataBlocks [ 47 ]:  0x40f	1039
    dataBlocks [ 48 ]:  0x40e	1038
    dataBlocks [ 49 ]:  0x40d	1037
    dataBlocks [ 50 ]:  0x40c	1036
    dataBlocks [ 51 ]:  0x40b	1035
    dataBlocks [ 52 ]:  0x40a	1034
    dataBlocks [ 53 ]:  0x409	1033
    dataBlocks [ 54 ]:  0x408	1032
    dataBlocks [ 55 ]:  0x407	1031
    dataBlocks [ 56 ]:  0x406	1030
    dataBlocks [ 57 ]:  0x405	1029
    dataBlocks [ 58 ]:  0x404	1028
    dataBlocks [ 59 ]:  0x403	1027
    dataBlocks [ 60 ]:  0x402	1026
    dataBlocks [ 61 ]:  0x401	1025
    dataBlocks [ 62 ]:  0x400	1024
    dataBlocks [ 63 ]:  0x3ff	1023
    dataBlocks [ 64 ]:  0x3fe	1022
    dataBlocks [ 65 ]:  0x3fd	1021
    dataBlocks [ 66 ]:  0x3fc	1020
    dataBlocks [ 67 ]:  0x3fb	1019
    dataBlocks [ 68 ]:  0x3fa	1018
    dataBlocks [ 69 ]:  0x3f9	1017
    dataBlocks [ 70 ]:  0x3f8	1016
    dataBlocks [ 71 ]:  0x3f7	1015

  r3 (non-zero):
  r5 (non-zero):

File extension block:
  0x000  type:		0x10		16
  0x004  headerKey:	0x43f		1087
  0x008  highSeq:	0x48		72
  0x00c  dataSize:	0x0		0
  0x010  firstData:	0x0		0
  0x014  checkSum:	0xfffeb6f2
     ->  calculated:	0xfffeb6f2 -> OK
  0x018  dataBlocks [ 72 ]:	(see below)
  0x138  r[45]:			(not used)
  0x1ec  info:		0x0		0
  0x1f0  nextSameHash:	0x0		0
  0x1f4  parent:	0x3f6		1014
  0x1f8  extension:	0x488		1160
  0x1fc  secType:	0xfffffffd	-3

  data blocks (non-zero):
    dataBlocks [  0 ]:  0x487	1159
    dataBlocks [  1 ]:  0x486	1158
    dataBlocks [  2 ]:  0x485	1157
    dataBlocks [  3 ]:  0x484	1156
    dataBlocks [  4 ]:  0x483	1155
    dataBlocks [  5 ]:  0x482	1154
    dataBlocks [  6 ]:  0x481	1153
    dataBlocks [  7 ]:  0x480	1152
    dataBlocks [  8 ]:  0x47f	1151
    dataBlocks [  9 ]:  0x47e	1150
    dataBlocks [ 10 ]:  0x47d	1149
    dataBlocks [ 11 ]:  0x47c	1148
    dataBlocks [ 12 ]:  0x47b	1147
    dataBlocks [ 13 ]:  0x47a	1146
    dataBlocks [ 14 ]:  0x479	1145
    dataBlocks [ 15 ]:  0x478	1144
    dataBlocks [ 16 ]:  0x477	1143
    dataBlocks [ 17 ]:  0x476	1142
    dataBlocks [ 18 ]:  0x475	1141
    dataBlocks [ 19 ]:  0x474	1140
    dataBlocks [ 20 ]:  0x473	1139
    dataBlocks [ 21 ]:  0x472	1138
    dataBlocks [ 22 ]:  0x471	1137
    dataBlocks [ 23 ]:  0x470	1136
    dataBlocks [ 24 ]:  0x46f	1135
    dataBlocks [ 25 ]:  0x46e	1134
    dataBlocks [ 26 ]:  0x46d	1133
    dataBlocks [ 27 ]:  0x46c	1132
    dataBlocks [ 28 ]:  0x46b	1131
    dataBlocks [ 29 ]:  0x46a	1130
    dataBlocks [ 30 ]:  0x469	1129
    dataBlocks [ 31 ]:  0x468	1128
    dataBlocks [ 32 ]:  0x467	1127
    dataBlocks [ 33 ]:  0x466	1126
    dataBlocks [ 34 ]:  0x465	1125
    dataBlocks [ 35 ]:  0x464	1124
    dataBlocks [ 36 ]:  0x463	1123
    dataBlocks [ 37 ]:  0x462	1122
    dataBlocks [ 38 ]:  0x461	1121
    dataBlocks [ 39 ]:  0x460	1120
    dataBlocks [ 40 ]:  0x45f	1119
    dataBlocks [ 41 ]:  0x45e	1118
    dataBlocks [ 42 ]:  0x45d	1117
    dataBlocks [ 43 ]:  0x45c	1116
    dataBlocks [ 44 ]:  0x45b	1115
    dataBlocks [ 45 ]:  0x45a	1114
    dataBlocks [ 46 ]:  0x459	1113
    dataBlocks [ 47 ]:  0x458	1112
    dataBlocks [ 48 ]:  0x457	1111
    dataBlocks [ 49 ]:  0x456	1110
    dataBlocks [ 50 ]:  0x455	1109
    dataBlocks [ 51 ]:  0x454	1108
    dataBlocks [ 52 ]:  0x453	1107
    dataBlocks [ 53 ]:  0x452	1106
    dataBlocks [ 54 ]:  0x451	1105
    dataBlocks [ 55 ]:  0x450	1104
    dataBlocks [ 56 ]:  0x44f	1103
    dataBlocks [ 57 ]:  0x44e	1102
    dataBlocks [ 58 ]:  0x44d	1101
    dataBlocks [ 59 ]:  0x44c	1100
    dataBlocks [ 60 ]:  0x44b	1099
    dataBlocks [ 61 ]:  0x44a	1098
    dataBlocks [ 62 ]:  0x449	1097
    dataBlocks [ 63 ]:  0x448	1096
    dataBlocks [ 64 ]:  0x447	1095
    dataBlocks [ 65 ]:  0x446	1094
    dataBlocks [ 66 ]:  0x445	1093
    dataBlocks [ 67 ]:  0x444	1092
    dataBlocks [ 68 ]:  0x443	1091
    dataBlocks [ 69 ]:  0x442	1090
    dataBlocks [ 70 ]:  0x441	1089
    dataBlocks [ 71 ]:  0x440	1088

File extension block:
  0x000  type:		0x10		16
  0x004  headerKey:	0x488		1160
  0x008  highSeq:	0x48		72
  0x00c  dataSize:	0x0		0
  0x010  firstData:	0x0		0
  0x014  checkSum:	0xfffea1d8
     ->  calculated:	0xfffea1d8 -> OK
  0x018  dataBlocks [ 72 ]:	(see below)
  0x138  r[45]:			(not used)
  0x1ec  info:		0x0		0
  0x1f0  nextSameHash:	0x0		0
  0x1f4  parent:	0x3f6		1014
  0x1f8  extension:	0x4d1		1233
  0x1fc  secType:	0xfffffffd	-3

  data blocks (non-zero):
    dataBlocks [  0 ]:  0x4d0	1232
    dataBlocks [  1 ]:  0x4cf	1231
    dataBlocks [  2 ]:  0x4ce	1230
    dataBlocks [  3 ]:  0x4cd	1229
    dataBlocks [  4 ]:  0x4cc	1228
    dataBlocks [  5 ]:  0x4cb	1227
    dataBlocks [  6 ]:  0x4ca	1226
    dataBlocks [  7 ]:  0x4c9	1225
    dataBlocks [  8 ]:  0x4c8	1224
    dataBlocks [  9 ]:  0x4c7	1223
    dataBlocks [ 10 ]:  0x4c6	1222
    dataBlocks [ 11 ]:  0x4c5	1221
    dataBlocks [ 12 ]:  0x4c4	1220
    dataBlocks [ 13 ]:  0x4c3	1219
    dataBlocks [ 14 ]:  0x4c2	1218
    dataBlocks [ 15 ]:  0x4c1	1217
    dataBlocks [ 16 ]:  0x4c0	1216
    dataBlocks [ 17 ]:  0x4bf	1215
    dataBlocks [ 18 ]:  0x4be	1214
    dataBlocks [ 19 ]:  0x4bd	1213
    dataBlocks [ 20 ]:  0x4bc	1212
    dataBlocks [ 21 ]:  0x4bb	1211
    dataBlocks [ 22 ]:  0x4ba	1210
    dataBlocks [ 23 ]:  0x4b9	1209
    dataBlocks [ 24 ]:  0x4b8	1208
    dataBlocks [ 25 ]:  0x4b7	1207
    dataBlocks [ 26 ]:  0x4b6	1206
    dataBlocks [ 27 ]:  0x4b5	1205
    dataBlocks [ 28 ]:  0x4b4	1204
    dataBlocks [ 29 ]:  0x4b3	1203
    dataBlocks [ 30 ]:  0x4b2	1202
    dataBlocks [ 31 ]:  0x4b1	1201
    dataBlocks [ 32 ]:  0x4b0	1200
    dataBlocks [ 33 ]:  0x4af	1199
    dataBlocks [ 34 ]:  0x4ae	1198
    dataBlocks [ 35 ]:  0x4ad	1197
    dataBlocks [ 36 ]:  0x4ac	1196
    dataBlocks [ 37 ]:  0x4ab	1195
    dataBlocks [ 38 ]:  0x4aa	1194
    dataBlocks [ 39 ]:  0x4a9	1193
    dataBlocks [ 40 ]:  0x4a8	1192
    dataBlocks [ 41 ]:  0x4a7	1191
    dataBlocks [ 42 ]:  0x4a6	1190
    dataBlocks [ 43 ]:  0x4a5	1189
    dataBlocks [ 44 ]:  0x4a4	1188
    dataBlocks [ 45 ]:  0x4a3	1187
    dataBlocks [ 46 ]:  0x4a2	1186
    dataBlocks [ 47 ]:  0x4a1	1185
    dataBlocks [ 48 ]:  0x4a0	1184
    dataBlocks [ 49 ]:  0x49f	1183
    dataBlocks [ 50 ]:  0x49e	1182
    dataBlocks [ 51 ]:  0x49d	1181
    dataBlocks [ 52 ]:  0x49c	1180
    dataBlocks [ 53 ]:  0x49b	1179
    dataBlocks [ 54 ]:  0x49a	1178
    dataBlocks [ 55 ]:  0x499	1177
    dataBlocks [ 56 ]:  0x498	1176
    dataBlocks [ 57 ]:  0x497	1175
    dataBlocks [ 58 ]:  0x496	1174
    dataBlocks [ 59 ]:  0x495	1173
    dataBlocks [ 60 ]:  0x494	1172
    dataBlocks [ 61 ]:  0x493	1171
    dataBlocks [ 62 ]:  0x492	1170
    dataBlocks [ 63 ]:  0x491	1169
    dataBlocks [ 64 ]:  0x490	1168
    dataBlocks [ 65 ]:  0x48f	1167
    dataBlocks [ 66 ]:  0x48e	1166
    dataBlocks [ 67 ]:  0x48d	1165
    dataBlocks [ 68 ]:  0x48c	1164
    dataBlocks [ 69 ]:  0x48b	1163
    dataBlocks [ 70 ]:  0x48a	1162
    dataBlocks [ 71 ]:  0x489	1161

File extension block:
  0x000  type:		0x10		16
  0x004  headerKey:	0x4d1		1233
  0x008  highSeq:	0x33		51
  0x00c  dataSize:	0x0		0
  0x010  firstData:	0x0		0
  0x014  checkSum:	0xfffefc28
     ->  calculated:	0xfffefc28 -> OK
  0x018  dataBlocks [ 72 ]:	(see below)
  0x138  r[45]:			(not used)
  0x1ec  info:		0x0		0
  0x1f0  nextSameHash:	0x0		0
  0x1f4  parent:	0x3f6		1014
  0x1f8  extension:	0x0		0
  0x1fc  secType:	0xfffffffd	-3

  data blocks (non-zero):
    dataBlocks [ 21 ]:  0x504	1284
    dataBlocks [ 22 ]:  0x503	1283
    dataBlocks [ 23 ]:  0x502	1282
    dataBlocks [ 24 ]:  0x501	1281
    dataBlocks [ 25 ]:  0x500	1280
    dataBlocks [ 26 ]:  0x4ff	1279
    dataBlocks [ 27 ]:  0x4fe	1278
    dataBlocks [ 28 ]:  0x4fd	1277
    dataBlocks [ 29 ]:  0x4fc	1276
    dataBlocks [ 30 ]:  0x4fb	1275
    dataBlocks [ 31 ]:  0x4fa	1274
    dataBlocks [ 32 ]:  0x4f9	1273
    dataBlocks [ 33 ]:  0x4f8	1272
    dataBlocks [ 34 ]:  0x4f7	1271
    dataBlocks [ 35 ]:  0x4f6	1270
    dataBlocks [ 36 ]:  0x4f5	1269
    dataBlocks [ 37 ]:  0x4f4	1268
    dataBlocks [ 38 ]:  0x4f3	1267
    dataBlocks [ 39 ]:  0x4f2	1266
    dataBlocks [ 40 ]:  0x4f1	1265
    dataBlocks [ 41 ]:  0x4f0	1264
    dataBlocks [ 42 ]:  0x4ef	1263
    dataBlocks [ 43 ]:  0x4ee	1262
    dataBlocks [ 44 ]:  0x4ed	1261
    dataBlocks [ 45 ]:  0x4ec	1260
    dataBlocks [ 46 ]:  0x4eb	1259
    dataBlocks [ 47 ]:  0x4ea	1258
    dataBlocks [ 48 ]:  0x4e9	1257
    dataBlocks [ 49 ]:  0x4e8	1256
    dataBlocks [ 50 ]:  0x4e7	1255
    dataBlocks [ 51 ]:  0x4e6	1254
    dataBlocks [ 52 ]:  0x4e5	1253
    dataBlocks [ 53 ]:  0x4e4	1252
    dataBlocks [ 54 ]:  0x4e3	1251
    dataBlocks [ 55 ]:  0x4e2	1250
    dataBlocks [ 56 ]:  0x4e1	1249
    dataBlocks [ 57 ]:  0x4e0	1248
    dataBlocks [ 58 ]:  0x4df	1247
    dataBlocks [ 59 ]:  0x4de	1246
    dataBlocks [ 60 ]:  0x4dd	1245
    dataBlocks [ 61 ]:  0x4dc	1244
    dataBlocks [ 62 ]:  0x4db	1243
    dataBlocks [ 63 ]:  0x4da	1242
    dataBlocks [ 64 ]:  0x4d9	1241
    dataBlocks [ 65 ]:  0x4d8	1240
    dataBlocks [ 66 ]:  0x4d7	1239
    dataBlocks [ 67 ]:  0x4d6	1238
    dataBlocks [ 68 ]:  0x4d5	1237
    dataBlocks [ 69 ]:  0x4d4	1236
    dataBlocks [ 70 ]:  0x4d3	1235
    dataBlocks [ 71 ]:  0x4d2	1234
EOF

$adf_show_metadata "$basedir/arccsh.adf" c/ >$actual
compare_with <<EOF

Opening image/device:	'$basedir/arccsh.adf'
Mounted volume:		0

Path:		c/

Directory block:
  0x000  type		0x2		2
  0x004  headerKey	0x372		882
  0x008  highSeq	0x0		0
  0x00c  hashTableSize	0x0		0
  0x010  r1		0x0
  0x014  checkSum	0xfe9ca61c
     ->  calculated:	0xfe9ca61c -> OK
  0x018  hashTable[72]:	(see below)
  0x138  r2[2]:		(see below)
  0x140  access		0x0
  0x144  r4		0x0
  0x148  commLen	0x0		0
  0x149  comment [ 80 ]:	
  0x199  r5[11]:	(see below)
  0x1a4  days		0x1ec8		7880
  0x1a8  mins		0x54b		1355
  0x1ac  ticks		0x37		55
  0x1b0  nameLen	0x1		1
  0x1b1  dirName:	c
  0x1d0  r6		0x0
  0x1d4  real		0x0		0
  0x1d8  nextLink	0x0		0
  0x1dc  r7[5]:		(see below)
  0x1f0  nextSameHash	0x0		0
  0x1f4  parent		0x370		880
  0x1f8  extension	0x0		0
  0x1fc  secType	0x2		2

Hashtable (non-zero):
  hashtable [  3 ]:		0x5ed		1517
  hashtable [  8 ]:		0x6df		1759
  hashtable [ 14 ]:		0x19		25
  hashtable [ 15 ]:		0x625		1573
  hashtable [ 17 ]:		0x1bc		444
  hashtable [ 18 ]:		0x16e		366
  hashtable [ 22 ]:		0x11		17
  hashtable [ 28 ]:		0x37e		894
  hashtable [ 31 ]:		0x390		912
  hashtable [ 34 ]:		0x399		921
  hashtable [ 52 ]:		0x384		900
  hashtable [ 60 ]:		0x60b		1547
  hashtable [ 63 ]:		0x37a		890
  hashtable [ 69 ]:		0xbf		191
EOF

$adf_show_metadata "$basedir/arccsh.adf" l >$actual
compare_with <<EOF

Opening image/device:	'$basedir/arccsh.adf'
Mounted volume:		0

Path:		l

Directory block:
  0x000  type		0x2		2
  0x004  headerKey	0x3f4		1012
  0x008  highSeq	0x0		0
  0x00c  hashTableSize	0x0		0
  0x010  r1		0x0
  0x014  checkSum	0xfe93c799
     ->  calculated:	0xfe93c799 -> OK
  0x018  hashTable[72]:	(see below)
  0x138  r2[2]:		(see below)
  0x140  access		0x0
  0x144  r4		0x0
  0x148  commLen	0x0		0
  0x149  comment [ 80 ]:	
  0x199  r5[11]:	(see below)
  0x1a4  days		0x1ec8		7880
  0x1a8  mins		0x536		1334
  0x1ac  ticks		0x90c		2316
  0x1b0  nameLen	0x1		1
  0x1b1  dirName:	l
  0x1d0  r6		0x0
  0x1d4  real		0x0		0
  0x1d8  nextLink	0x0		0
  0x1dc  r7[5]:		(see below)
  0x1f0  nextSameHash	0x0		0
  0x1f4  parent		0x370		880
  0x1f8  extension	0x0		0
  0x1fc  secType	0x2		2

Hashtable (non-zero):
  hashtable [ 48 ]:		0x3f5		1013
EOF

read status < $status && test "x$status" = xsuccess
