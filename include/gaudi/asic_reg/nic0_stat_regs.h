/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_NIC0_STAT_REGS_H_
#define ASIC_REG_NIC0_STAT_REGS_H_

/*
 *****************************************
 *   NIC0_STAT (Prototype: NIC_STAT)
 *****************************************
 */

#define mmNIC0_STAT_DATA_HI_REG                                      0xCC4000

#define mmNIC0_STAT_STATN_STATUS                                     0xCC4004

#define mmNIC0_STAT_STATN_CONFIG                                     0xCC4008

#define mmNIC0_STAT_STATN_CONTROL                                    0xCC400C

#define mmNIC0_STAT_ETHERSTATSOCTETS                                 0xCC4100

#define mmNIC0_STAT_OCTETSRECEIVEDOK                                 0xCC4104

#define mmNIC0_STAT_AALIGNMENTERRORS                                 0xCC4108

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESRECEIVED                      0xCC410C

#define mmNIC0_STAT_AFRAMETOOLONGERRORS                              0xCC4110

#define mmNIC0_STAT_AINRANGELENGTHERRORS                             0xCC4114

#define mmNIC0_STAT_AFRAMESRECEIVEDOK                                0xCC4118

#define mmNIC0_STAT_VLANRECEIVEDOK                                   0xCC411C

#define mmNIC0_STAT_AFRAMECHECKSEQUENCEERRORS                        0xCC4120

#define mmNIC0_STAT_IFINERRORS                                       0xCC4124

#define mmNIC0_STAT_IFINUCASTPKTS                                    0xCC4128

#define mmNIC0_STAT_IFINMULTICASTPKTS                                0xCC412C

#define mmNIC0_STAT_IFINBROADCASTPKTS                                0xCC4130

#define mmNIC0_STAT_ETHERSTATSDROPEVENTS                             0xCC4134

#define mmNIC0_STAT_ETHERSTATSUNDERSIZEPKTS                          0xCC4138

#define mmNIC0_STAT_ETHERSTATSPKTS                                   0xCC413C

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS                           0xCC4140

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS                      0xCC4144

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS                     0xCC4148

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS                     0xCC414C

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS                    0xCC4150

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS                   0xCC4154

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS                    0xCC4158

#define mmNIC0_STAT_ETHERSTATSOVERSIZEPKTS                           0xCC415C

#define mmNIC0_STAT_ETHERSTATSJABBERS                                0xCC4160

#define mmNIC0_STAT_ETHERSTATSFRAGMENTS                              0xCC4164

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_0                       0xCC4168

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_1                       0xCC416C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_2                       0xCC4170

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_3                       0xCC4174

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_4                       0xCC4178

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_5                       0xCC417C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_6                       0xCC4180

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_7                       0xCC4184

#define mmNIC0_STAT_AMACCONTROLFRAMESRECEIVED                        0xCC4188

#define mmNIC0_STAT_ETHERSTATSOCTETS_1                               0xCC418C

#define mmNIC0_STAT_OCTETSRECEIVEDOK_1                               0xCC4190

#define mmNIC0_STAT_AALIGNMENTERRORS_1                               0xCC4194

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESRECEIVED_1                    0xCC4198

#define mmNIC0_STAT_AFRAMETOOLONGERRORS_1                            0xCC419C

#define mmNIC0_STAT_AINRANGELENGTHERRORS_1                           0xCC41A0

#define mmNIC0_STAT_AFRAMESRECEIVEDOK_1                              0xCC41A4

#define mmNIC0_STAT_VLANRECEIVEDOK_1                                 0xCC41A8

#define mmNIC0_STAT_AFRAMECHECKSEQUENCEERRORS_1                      0xCC41AC

#define mmNIC0_STAT_IFINERRORS_1                                     0xCC41B0

#define mmNIC0_STAT_IFINUCASTPKTS_1                                  0xCC41B4

#define mmNIC0_STAT_IFINMULTICASTPKTS_1                              0xCC41B8

#define mmNIC0_STAT_IFINBROADCASTPKTS_1                              0xCC41BC

#define mmNIC0_STAT_ETHERSTATSDROPEVENTS_1                           0xCC41C0

#define mmNIC0_STAT_ETHERSTATSUNDERSIZEPKTS_1                        0xCC41C4

#define mmNIC0_STAT_ETHERSTATSPKTS_1                                 0xCC41C8

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_1                         0xCC41CC

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_1                    0xCC41D0

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_1                   0xCC41D4

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_1                   0xCC41D8

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_1                  0xCC41DC

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_1                 0xCC41E0

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_1                  0xCC41E4

#define mmNIC0_STAT_ETHERSTATSOVERSIZEPKTS_1                         0xCC41E8

#define mmNIC0_STAT_ETHERSTATSJABBERS_1                              0xCC41EC

#define mmNIC0_STAT_ETHERSTATSFRAGMENTS_1                            0xCC41F0

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_0_1                     0xCC41F4

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_1_1                     0xCC41F8

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_2_1                     0xCC41FC

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_3_1                     0xCC4200

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_4_1                     0xCC4204

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_5_1                     0xCC4208

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_6_1                     0xCC420C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_7_1                     0xCC4210

#define mmNIC0_STAT_AMACCONTROLFRAMESRECEIVED_1                      0xCC4214

#define mmNIC0_STAT_ETHERSTATSOCTETS_2                               0xCC4218

#define mmNIC0_STAT_OCTETSRECEIVEDOK_2                               0xCC421C

#define mmNIC0_STAT_AALIGNMENTERRORS_2                               0xCC4220

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESRECEIVED_2                    0xCC4224

#define mmNIC0_STAT_AFRAMETOOLONGERRORS_2                            0xCC4228

#define mmNIC0_STAT_AINRANGELENGTHERRORS_2                           0xCC422C

#define mmNIC0_STAT_AFRAMESRECEIVEDOK_2                              0xCC4230

#define mmNIC0_STAT_VLANRECEIVEDOK_2                                 0xCC4234

#define mmNIC0_STAT_AFRAMECHECKSEQUENCEERRORS_2                      0xCC4238

#define mmNIC0_STAT_IFINERRORS_2                                     0xCC423C

#define mmNIC0_STAT_IFINUCASTPKTS_2                                  0xCC4240

#define mmNIC0_STAT_IFINMULTICASTPKTS_2                              0xCC4244

#define mmNIC0_STAT_IFINBROADCASTPKTS_2                              0xCC4248

#define mmNIC0_STAT_ETHERSTATSDROPEVENTS_2                           0xCC424C

#define mmNIC0_STAT_ETHERSTATSUNDERSIZEPKTS_2                        0xCC4250

#define mmNIC0_STAT_ETHERSTATSPKTS_2                                 0xCC4254

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_2                         0xCC4258

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_2                    0xCC425C

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_2                   0xCC4260

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_2                   0xCC4264

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_2                  0xCC4268

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_2                 0xCC426C

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_2                  0xCC4270

#define mmNIC0_STAT_ETHERSTATSOVERSIZEPKTS_2                         0xCC4274

#define mmNIC0_STAT_ETHERSTATSJABBERS_2                              0xCC4278

#define mmNIC0_STAT_ETHERSTATSFRAGMENTS_2                            0xCC427C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_0_2                     0xCC4280

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_1_2                     0xCC4284

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_2_2                     0xCC4288

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_3_2                     0xCC428C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_4_2                     0xCC4290

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_5_2                     0xCC4294

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_6_2                     0xCC4298

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_7_2                     0xCC429C

#define mmNIC0_STAT_AMACCONTROLFRAMESRECEIVED_2                      0xCC42A0

#define mmNIC0_STAT_ETHERSTATSOCTETS_3                               0xCC42A4

#define mmNIC0_STAT_OCTETSRECEIVEDOK_3                               0xCC42A8

#define mmNIC0_STAT_AALIGNMENTERRORS_3                               0xCC42AC

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESRECEIVED_3                    0xCC42B0

#define mmNIC0_STAT_AFRAMETOOLONGERRORS_3                            0xCC42B4

#define mmNIC0_STAT_AINRANGELENGTHERRORS_3                           0xCC42B8

#define mmNIC0_STAT_AFRAMESRECEIVEDOK_3                              0xCC42BC

#define mmNIC0_STAT_VLANRECEIVEDOK_3                                 0xCC42C0

#define mmNIC0_STAT_AFRAMECHECKSEQUENCEERRORS_3                      0xCC42C4

#define mmNIC0_STAT_IFINERRORS_3                                     0xCC42C8

#define mmNIC0_STAT_IFINUCASTPKTS_3                                  0xCC42CC

#define mmNIC0_STAT_IFINMULTICASTPKTS_3                              0xCC42D0

#define mmNIC0_STAT_IFINBROADCASTPKTS_3                              0xCC42D4

#define mmNIC0_STAT_ETHERSTATSDROPEVENTS_3                           0xCC42D8

#define mmNIC0_STAT_ETHERSTATSUNDERSIZEPKTS_3                        0xCC42DC

#define mmNIC0_STAT_ETHERSTATSPKTS_3                                 0xCC42E0

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_3                         0xCC42E4

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_3                    0xCC42E8

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_3                   0xCC42EC

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_3                   0xCC42F0

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_3                  0xCC42F4

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_3                 0xCC42F8

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_3                  0xCC42FC

#define mmNIC0_STAT_ETHERSTATSOVERSIZEPKTS_3                         0xCC4300

#define mmNIC0_STAT_ETHERSTATSJABBERS_3                              0xCC4304

#define mmNIC0_STAT_ETHERSTATSFRAGMENTS_3                            0xCC4308

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_0_3                     0xCC430C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_1_3                     0xCC4310

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_2_3                     0xCC4314

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_3_3                     0xCC4318

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_4_3                     0xCC431C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_5_3                     0xCC4320

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_6_3                     0xCC4324

#define mmNIC0_STAT_ACBFCPAUSEFRAMESRECEIVED_7_3                     0xCC4328

#define mmNIC0_STAT_AMACCONTROLFRAMESRECEIVED_3                      0xCC432C

#define mmNIC0_STAT_ETHERSTATSOCTETS_4                               0xCC4330

#define mmNIC0_STAT_OCTETSTRANSMITTEDOK                              0xCC4334

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESTRANSMITTED                   0xCC4338

#define mmNIC0_STAT_AFRAMESTRANSMITTEDOK                             0xCC433C

#define mmNIC0_STAT_VLANTRANSMITTEDOK                                0xCC4340

#define mmNIC0_STAT_IFOUTERRORS                                      0xCC4344

#define mmNIC0_STAT_IFOUTUCASTPKTS                                   0xCC4348

#define mmNIC0_STAT_IFOUTMULTICASTPKTS                               0xCC434C

#define mmNIC0_STAT_IFOUTBROADCASTPKTS                               0xCC4350

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_4                         0xCC4354

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_4                    0xCC4358

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_4                   0xCC435C

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_4                   0xCC4360

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_4                  0xCC4364

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_4                 0xCC4368

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_4                  0xCC436C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_0                    0xCC4370

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_1                    0xCC4374

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_2                    0xCC4378

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_3                    0xCC437C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_4                    0xCC4380

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_5                    0xCC4384

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_6                    0xCC4388

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_7                    0xCC438C

#define mmNIC0_STAT_AMACCONTROLFRAMESTRANSMITTED                     0xCC4390

#define mmNIC0_STAT_ETHERSTATSPKTS_4                                 0xCC4394

#define mmNIC0_STAT_ETHERSTATSOCTETS_5                               0xCC4398

#define mmNIC0_STAT_OCTETSTRANSMITTEDOK_1                            0xCC439C

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESTRANSMITTED_1                 0xCC43A0

#define mmNIC0_STAT_AFRAMESTRANSMITTEDOK_1                           0xCC43A4

#define mmNIC0_STAT_VLANTRANSMITTEDOK_1                              0xCC43A8

#define mmNIC0_STAT_IFOUTERRORS_1                                    0xCC43AC

#define mmNIC0_STAT_IFOUTUCASTPKTS_1                                 0xCC43B0

#define mmNIC0_STAT_IFOUTMULTICASTPKTS_1                             0xCC43B4

#define mmNIC0_STAT_IFOUTBROADCASTPKTS_1                             0xCC43B8

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_5                         0xCC43BC

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_5                    0xCC43C0

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_5                   0xCC43C4

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_5                   0xCC43C8

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_5                  0xCC43CC

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_5                 0xCC43D0

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_5                  0xCC43D4

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_0_1                  0xCC43D8

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_1_1                  0xCC43DC

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_2_1                  0xCC43E0

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_3_1                  0xCC43E4

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_4_1                  0xCC43E8

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_5_1                  0xCC43EC

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_6_1                  0xCC43F0

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_7_1                  0xCC43F4

#define mmNIC0_STAT_AMACCONTROLFRAMESTRANSMITTED_1                   0xCC43F8

#define mmNIC0_STAT_ETHERSTATSPKTS_5                                 0xCC43FC

#define mmNIC0_STAT_ETHERSTATSOCTETS_6                               0xCC4400

#define mmNIC0_STAT_OCTETSTRANSMITTEDOK_2                            0xCC4404

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESTRANSMITTED_2                 0xCC4408

#define mmNIC0_STAT_AFRAMESTRANSMITTEDOK_2                           0xCC440C

#define mmNIC0_STAT_VLANTRANSMITTEDOK_2                              0xCC4410

#define mmNIC0_STAT_IFOUTERRORS_2                                    0xCC4414

#define mmNIC0_STAT_IFOUTUCASTPKTS_2                                 0xCC4418

#define mmNIC0_STAT_IFOUTMULTICASTPKTS_2                             0xCC441C

#define mmNIC0_STAT_IFOUTBROADCASTPKTS_2                             0xCC4420

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_6                         0xCC4424

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_6                    0xCC4428

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_6                   0xCC442C

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_6                   0xCC4430

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_6                  0xCC4434

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_6                 0xCC4438

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_6                  0xCC443C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_0_2                  0xCC4440

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_1_2                  0xCC4444

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_2_2                  0xCC4448

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_3_2                  0xCC444C

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_4_2                  0xCC4450

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_5_2                  0xCC4454

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_6_2                  0xCC4458

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_7_2                  0xCC445C

#define mmNIC0_STAT_AMACCONTROLFRAMESTRANSMITTED_2                   0xCC4460

#define mmNIC0_STAT_ETHERSTATSPKTS_6                                 0xCC4464

#define mmNIC0_STAT_ETHERSTATSOCTETS_7                               0xCC4468

#define mmNIC0_STAT_OCTETSTRANSMITTEDOK_3                            0xCC446C

#define mmNIC0_STAT_APAUSEMACCTRLFRAMESTRANSMITTED_3                 0xCC4470

#define mmNIC0_STAT_AFRAMESTRANSMITTEDOK_3                           0xCC4474

#define mmNIC0_STAT_VLANTRANSMITTEDOK_3                              0xCC4478

#define mmNIC0_STAT_IFOUTERRORS_3                                    0xCC447C

#define mmNIC0_STAT_IFOUTUCASTPKTS_3                                 0xCC4480

#define mmNIC0_STAT_IFOUTMULTICASTPKTS_3                             0xCC4484

#define mmNIC0_STAT_IFOUTBROADCASTPKTS_3                             0xCC4488

#define mmNIC0_STAT_ETHERSTATSPKTS64OCTETS_7                         0xCC448C

#define mmNIC0_STAT_ETHERSTATSPKTS65TO127OCTETS_7                    0xCC4490

#define mmNIC0_STAT_ETHERSTATSPKTS128TO255OCTETS_7                   0xCC4494

#define mmNIC0_STAT_ETHERSTATSPKTS256TO511OCTETS_7                   0xCC4498

#define mmNIC0_STAT_ETHERSTATSPKTS512TO1023OCTETS_7                  0xCC449C

#define mmNIC0_STAT_ETHERSTATSPKTS1024TO1518OCTETS_7                 0xCC44A0

#define mmNIC0_STAT_ETHERSTATSPKTS1519TOMAXOCTETS_7                  0xCC44A4

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_0_3                  0xCC44A8

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_1_3                  0xCC44AC

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_2_3                  0xCC44B0

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_3_3                  0xCC44B4

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_4_3                  0xCC44B8

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_5_3                  0xCC44BC

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_6_3                  0xCC44C0

#define mmNIC0_STAT_ACBFCPAUSEFRAMESTRANSMITTED_7_3                  0xCC44C4

#define mmNIC0_STAT_AMACCONTROLFRAMESTRANSMITTED_3                   0xCC44C8

#define mmNIC0_STAT_ETHERSTATSPKTS_7                                 0xCC44CC

#endif /* ASIC_REG_NIC0_STAT_REGS_H_ */
