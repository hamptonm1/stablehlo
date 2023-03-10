// RUN: stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt -inline | stablehlo-translate --interpret
// RUN: diff <(stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt) <(stablehlo-opt %s)
// RUN: diff <(stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt) <(stablehlo-opt %s)

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0 = call @inputs() : () -> tensor<20x20xf32>
    %1 = call @expected() : () -> tensor<20x20xf32>
    %2 = stablehlo.exponential %0 : tensor<20x20xf32>
    %3 = stablehlo.custom_call @check.eq(%2, %1) : (tensor<20x20xf32>, tensor<20x20xf32>) -> tensor<i1>
    return %3 : tensor<i1>
  }
  func.func private @inputs() -> tensor<20x20xf32> {
    %0 = stablehlo.constant dense<"0x31C865BF1B4EC7BE6E9C8C3EC669274064C05EBEFB51C23F3DB788BE17E7A7BFB2D8923F57A2343F9B638FBF0D534ABF6FE64DC0462F20BF5EF1C340AC3E9540DA397BC07945F83F6B9B5CC0E79E0BC08B079CC03DC80EC06D8F25BF5676BD3F82481FC1D7D672C019378DC02E80BCC00F5417BE155367BF326487BFDF64694020D005C193B08B40DF70AEBE755975C02209154032D9D13F216D3AC00EF1E63F2CC2964013DFF73EB29248C017C65FBE1E9B41C0260316C0C65C6540804E8CC09CD100BF92AEA63F70E3783F2B4EEDBFF8503CC06F85DCBEEA7EEFBF828AE03F5F142DC07E73CB40654046C07DBCD4BF58CC8140BB0F4B40669B13BF36AF87BF6277E43F6948B440569A6E408A2F8F405A0E49C0187D00C07C3F16C0410B1D4010EDBA40C8F3BD3F1D0ADFC02F39BABF27CB84BFAE1320C0C96F534039CCE6BF202D81C03D0C8C3F2B7C1FBF831EEBBE0620D8BFD30F0840F27F08C070ABDE40A13A0C402B293A40986BC7C02C6254C0000274C08CBA6740FC6425BECD434A40CEFAB0BF8D6C35408E8805BE96ED93BF58B9DBBF1FC80740F1C77D4018A024C0087F14BFECF2C2BFB9E0063FCEE5AE3F1A3254C09F44E33F0CEF2440BD0CEB3E2171B7C06A6DC0BFF61E3A40863804C0C0E7CABF5EAB843F2F0DFC3FF898F13F512DE33DE103E1BF24AD2F3F1C078ABED8E9133ECF2F0C3F09FB5EC083069E3FD2783D3FA0FF5EC0CD653D40B4AB8BC09ABAF03FCCD2D0C0ED8413C0915B8F408E5C7DC0AFC81FC001CA97BFC626DCBFB2D13D3D4F71C4BF19BF07404292A74024C49040116EA33E188DC9BF2849274067D09A3FC7BEEA3F6E5296407426073FBAAFC1BF38A5BABF1708EA3E03F384402AE94340E3F041BFB9510BBF2DED87C015DA22C0E94B373FDA4D364060B4804013CD943F4D1719C08AC152BD115F283F598ABE3E1A69AB3DADB2E4BF82C58EC064756E3FA5DE2F3DB6B6C03DD9ABBF4096669D4009CB76BD2F18B13EF51913C07F8882BF2E6720404EBB96402E41343F2827E4C05B60C7BE1892D140DF2BFE3F7C9686407CABB83F0065413F10A2B1BF41DF72406E9D10BFF80F2D40C898D3BF3DC45BBF22FD143F9F0991C036C65A3EC20E2B40A863E3408F916D3EE512E63F552C98402837D3BD4BC3E63F9C8FEC3F0E5AB5BFB33A0AC0A1DE3440FE505D40C81550BFEB0E50C0319687400FDE4FC0A43F0540F995BBBF08D63B3D129C874059720E4091822E40EFC535C0C86E063EBCAD71BF94E81BC0E5FE203F6E54EA3F823EC83F6A29A7BE08371CBF6172B83F0BDC76C0253AA23EBE9D37BF03199040CE5962400A029EC038420940F6642BBF8883604078ABB4BF1AD608405C4C013F00510E40816C733F526073BF1E30104070DBC1C0B8D5783F37F42D40DC522540F725BB3F45568840101A6FC0B89F7E40CE4F1D4075A10140B9E2C5BF542D2EC0EFE6A3BE9D54004017F4833ECD3817C0BDD45B3F9A85B5C034358CBFB8316EC0856DFC3D5B4D8C3FC02A444001807C407BCF2B400B184AC074003340B1018DBF8FC28D3F1E1E6F4098F35B3ECD93E2404880A8BEFBC797BF07C20940B0395E404F4DD6BFB3B695BE34BB9D409A76CBBFEC9A244099E5C6BFF6EBBE3F378A0EC035FCD73FD806BD3DA6E4B8C0047882409C008BC0380A63BFC66EEABE484D56BED0A75C3FC93BD63FCE72033F1C4E903F16B16740503A43C0740E11C053EFC5BCE78065C096BB9F40DA8B02BF186C5C4092291C403BAD5D407BA2F5BF1081183ECA85B1BF2F19EDC0C9681340F1CBABBF533C8AC0AE7F41403771683FE66C2AC004B0943E1EB41540B7DB05C015B40840489FAE40DEB1D73E349A80404C4F89C09C9FAE40EC166EC0C46D0D41E4DB2BC0BA2937C0915204403A0EB9BF67860EC0AFE262C085543B4075F3574010A367BF838EC13E11AC6140112703BF9D3B7FBCF844DC3F56D297BC0C5E85C099CC8A3F81CF6440293501C05FEBA13FC12A9FC0C718B73E8D50923F4616B43F63279BC03B61963F8336603FB2251B40D7089DBF9D0C02402A889AC043B929BFDC372840A4601940FB117B3E84471ABE6FDC3D4053F7ACBED440953D6C563641278DDEBF10098EBF85681840D46352BF10F8473FDF45B5BF702F88BF04A032BF8D781B4002EE884028EAE53F84442EC0DB4004C0A62E743FB864D13FBF2FACBEDB5964C04B51DCBF0F6438C0CA5C8DC05238F5BF71BF9E3F2A15124098B158BFE6B2CC3B"> : tensor<20x20xf32>
    return %0 : tensor<20x20xf32>
  }
  func.func private @expected() -> tensor<20x20xf32> {
    %0 = stablehlo.constant dense<"0xB3AAD03E1A742D3F2974A83F74DB5A41E6F34D3F93099240FF01443F61E8893E1D914940AD9A01406E04A73EBC4AE83ED41D243D99ED083F182AE4430A19D44275A9A13C7098DE40326C023D2125E73DDEF4F93B1E01DC3D3415063F44998C4042254738204CB83C078F463C2438353BA2D45C3FCA69CF3E2AC9B13E78671942149A7439C1589D42B615363F7035B13C043B24418ADFA440EB7B5E3DDC68C240205FDE425BB6CF3F8E5C323D4ABF4D3FDADF463DB980C43DE6091042F8464C3CAAC61A3F4F5A6B40283429405E60203E8802583DB269263FC4A71D3E64EDB8407609893D3840104437F3383D1850423E320467422800BF41D7D20F3F1761B13EABAFBE40F4DD8B435F692642C082AF423C05313D7E87093ED0C7C33D261F3A411823AC4353238D400454763A74086F3E506EB53E84E8A73D64B4D941ACBD283E66A0903CDC233F40874D093F34BC213F663C3D3E8718064130B2F23D4B7F8344201E0F413CAC9241BAD3003B344E143D78F6B43CC676154262D1593F53A3BC418277803E8E358841A2B3603FD232A13E48FD373E9F8205414AF35242A1639C3D2D530F3F12485F3E71C8D83F52F47A40C2BD143DCEE8BC406C8A52414993CA3FA641543B40B9633EDA94924117BD013E3AD3513E006F34406944E5406E49D34019048F3FF289303E9C3CFE3F9481433F27E4933F5853DD3F4759FB3C62F65B40182806404147FB3C1C489A419B60503C9EDBD140800DC03AF64ECC3DE174B0424A5B9C3CBAADA83DA1689C3E4060373E4A12863FEBB05C3ECD6F0541CA073C439665B8429921B03F810E543E066C5A4133835640F745C840E45BDB428603D93FA37E613E093F6E3E432CCA3F1DEC7E422ECFAA416106F03E538E143F2C3E6A3C8FC8A03D88F502405E188A412D415F4221AB4C401946BB3D3029733FAA15F73F70B5B93F9C2C8B3F28882B3E0F223D3CD97222409A9D853F27A18C3F24A7C743A7D50843EA07713F2CE5B43F8FA5CD3DCDA9B83EBC2644416D2FDE428669014034F3513AEB6D2D3FA4A62E448A18E94067288642936E8740633A084022A07F3E15E43142B283113FC20D6F41E70C443E02FED83E5711E53FF033303CDA7C9E3F84AE674108659844656CA13F9418C140476CE842F5E9663F6723C2407122CB40494E783EB139EC3DD00887416C0BFE41851FE33EB3AC1E3D58698A4205261F3D4054004190806C3EAD01863FC8828A42EA291441CB85744123436F3D00F5913F1031C73E5035B33D4011F03FD79FC74078F4984028B1383F66110B3F353287408E13AD3CCBB7AF3F3DE7F93EB495B442F26A094261F8EA3B8AA00841CB0F033F8D870542DEA1793E7EBA0741FA1BD43FCBDC134154A1254008E0C53E10401841B649193B172B294046687241C6D3534170148A40F7B18D425861C33C38BF5542ECE63A41458DF24092375A3E01B5863D22DF393F90ACED4017A1A53FE9D2C03D4D0C1740C161613B6538AB3EB32BC63C4DCA903F32853F40937EAB411FC54E424C6C6A41462C2E3D7C268341B627AA3ECBB6414068C1274286AB9E3F9B939444A235383F1A6B9C3E74B20941D8D6004254F33F3E30183F3F5B410A4391E9503E62765141F97F583E04368E40BCD6DC3D68F8AC4066608C3F67D84A3B9EE86B4272C6543CF5E9D23EC2F3213FC1A84F3F0A891740929EAA40F9E5D53FB69A4540AF601542E1E5413DDC52D43D7FE3793F4DFEE23CCB2D13432FBC193F2485FA4153933741977AFF41C745163E448E943F9FD87F3EC6BF1E3A161C20418AC6853E5AEF593CD97DA441E7AC1E40F0D68E3DD021AB3F14F4254132ECFC3D6B72074144616A43750FC33FE08A5E423355603CAC636A43BE7EC63C31A1D745F7AC8B3D5B1F6A3D9CF8FC404B39713EE4E3DC3DB478EC3C96609541889DE9410829CF3E56CEBA3FE0F70742165F193FFB0A7C3F94DBB2409D4C7B3FBEBE7D3CDE483D4053CD0E422AFE073EC4C16240A39CE23B4407B73F2ABB484034AB8240FB71003C5D354F40E1A71940CAAF3441ED20963EB624F44056F7023C79EB033FA5A05D413CC22F41E090A33FF2315C3F21679B412C9C363F39AD893FFCC6AD4746F8333E02CBA83EF81D2D416116E13E9DC50B407375783EBEAFB03EC6D2FE3E4D9A35411F58904229DBC0403C84863D34AC013E1F1F2640C649A44068E3363F3E1EE73C6023373E70AC653DB0A5453C9DC2163E14355D4032D31C41CF9CDB3E57CD803F"> : tensor<20x20xf32>
    return %0 : tensor<20x20xf32>
  }
}
