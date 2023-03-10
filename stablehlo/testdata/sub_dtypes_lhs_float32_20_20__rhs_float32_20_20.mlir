// RUN: stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt -inline | stablehlo-translate --interpret
// RUN: diff <(stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt) <(stablehlo-opt %s)
// RUN: diff <(stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt) <(stablehlo-opt %s)

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0:2 = call @inputs() : () -> (tensor<20x20xf32>, tensor<20x20xf32>)
    %1 = call @expected() : () -> tensor<20x20xf32>
    %2 = stablehlo.subtract %0#0, %0#1 : tensor<20x20xf32>
    %3 = stablehlo.custom_call @check.eq(%2, %1) : (tensor<20x20xf32>, tensor<20x20xf32>) -> tensor<i1>
    return %3 : tensor<i1>
  }
  func.func private @inputs() -> (tensor<20x20xf32>, tensor<20x20xf32>) {
    %0 = stablehlo.constant dense<"0x2F672D4004B95C401AB196BFBFD9C83F939C953ED1A92CC0F511F5BF33D8B6BEAE6D52C02C4AE2BF86D7A8C0213F96C0729B5CC0757ADBBFA2C104C03C924CBFB937C84080C53E409541464068F396C02C0218C056C7B840D23B5440E84D1640DB42D7BE8FD613401D8162C06689A73F5521A9401046F8BF2C27F5BFE759A0BF5A46BDBF908FBA3E43776EBF26C21D4008ED63C0DC7D13C0131271BE0E180ABF51D8C23F6B1B723F1E5D67C0BCDA214070208BBF1AF632C0140790BF4574DC3D367F25402BD92FC003DA42C0CE29A2C0E5D74CBC4E70463E99D19F401F1528403FADFABF6A5E8540C397A6BE94198740CC9A094051283C40F8723740901991C08FB066405FB336C028BD26401A3BE63FF52E4B405A751740E07C7FBF737137C00AE4AD4014B4B6BE42D14D40BABFE3403C0F14408A7AB13F946DA44045E016C08DE6DEBDB60E35C05105C53F4241443FE50A9E3F643A943F607F103EA0EC4ABFB8BEAC403EB6A23FEAC4A6C01B206D3FDD8B06C0E2DB4CC0E6F5A4C094CBAEBE1042C1C0DF3F7CBF3AB3A73FD18DD13FF087F2BFA08C303F0D295BC0789F833EF6FA34C06E828FC0478022C0793810C0FF2FD83E901A1EC0B11823404FF713C0357F92C01D8B35C075D312C0D35A873D8E6FC840949E6E4010009AC00FE4B03E44EDB9BE7CE667BF1B2C3DBF882237BCE00CBA3F45D429BFEDDA3CC0B4F44B407E4F873E0C37F33F1B690C4002A9693F261FBA408DB734C066925CBF358B51BF3507EEBE0CA3943FD8CB62BFD4D7DF3E17D64DC0AC46E63FE7BCB7BD95F102C0A24383C0548204407F0ABAC05B6511C08B6FA73F634B814005EAAE4034B3ACC03880DDC0653E9040F7625A4021068840ED103EBF3F5D12C0C7B647C028D39CBE0BEAD33F1F5F174015DA303FFBB4C4C00CDE42400DE87BC082642DC01AB396409907F23ED43045BFBD54B13FF6449240512B7AC03C56CDC01A3E93C0959A913F803CE7BF663A02C0574EC3403F04603F3254544008D2093E5B8CA4C039A68E401AD997C0620FE6BFDF5A8BBF6D71F63F62BC18C020A87040386516BF8B7897C0CBFC28406E228BBD1C4082C0471E933F2EF72CC011F43240718AAF3C0D9461C0493E8A3FE1FC573C45AA473ED6C60F40DB80A23F29C016C0C20782BF0A9814BFC68A9EBF040974C0DB461ABD8922A9BF1774CE3E47C6FEBF44B9FABFA5FFAB3EDE78BBBF165E213F2AD9903E28DD994046B39F40A5B086BF324832C0A776B4BF844D03C0A40216C00AB784C041207F40EF4CC7BE1838B83E43B69DC064B4E33ECCE91BC071F81BC07C7419403CA6A940B58598C0A9B0273ED9C6A240808CB4BD3A4EB03F1306F9BF07FA69BD3A9FCCBF0C369EBE8480FD3FA136A9C0E9E1D7BF64DA573F966217C07DB7074064B132C07CB383BFE5135D3FED9733C0AFA80E3F7ED385C0E99491C09DA35D40BFA3EEBFCC24963F19C06EBFBAF6AA3F41E84D3FA9359BC0D14F1BBFB98947C07D5DDABFC9A91C40CBF46C3E9D25D63F2881A8BE0BEA4DBFE333F93F615D843F13A938C0569B7F3E12A34EBEC8D97FC0D19576403DDFF9BFCA5A8DBF641D0ABE9395FE3E9638CE3FEF0C3A3F8B1B88C06DF651BF10052C40F8F654C02983C3402F0BE3BD1C4E03C1D7998D40A51640401D3B99BFDD2618BFE7B17740E705A140273C1040952789BFF9E3863F8A49513D49DCC2407190D74003E1653F72DE56C05F87D7C0E4C22B40473BB13C63CA03404D2659C06E9B083F744B5EBF2C60933ECB429EC03EB387403B9ED4BFA4829FC005DAFEBFB6C3D9BFDBE8A73FD99BFABFBA841140D68A12C0184557C0E0BFAAC040BCC33FCF3C9BC064202FC0305016C056430F40F4205C3EF15C4A405D4881BEA1F625401E60403E5F5EB13F76B0933FE91A0C40C4AA023EA6021AC0F96F4DC0D9C9D43F84493A40311A63BE448B473F180E0B40DC67A2BFEE8726408DA458C0BCCBE3BF379186C0D162434065B88FC0FE39D43F5EAD433FFD8E863E3988B33FD73A0A40B142913F0DF6AE3F2F7A3D405570A7C003F0623FB9147AC02407FFBEE03343C02DEBEB3FCB8EE43FA4C628C054D00F3FDACE9440669131C0A0C68C4024ADEDBEDC4E853F9D75C3400CD00D40792655C08132803F87E44B3FF686803F5E0C224040D182BF044E94BF841537C02CC8F5C09D99C4BFB8F992C0CEDF9CBF3EB66C3FF5A53AC02D6694BD1AA989C0CE0873C06C57F2BF44640FC02E79A140BBE6863F"> : tensor<20x20xf32>
    %1 = stablehlo.constant dense<"0x8BA18BC0D3E3A5400B368A3E054F5E40688BBEBF36CA27BC1355833F83D7E0BF049E63C013E668C006C6423EB4F966C0F6B2A6BF703F99BF5A5E8340BA556BBF66D745BF7B710CC019578E3E1D7A2DC01C648B40A323A93FAA262B3FAF92F13FF4ADCBC030F07340089AC7BF8B604AC075BFB93F8B43A04018888CBF092EF33E2CD0ECBD7A9D0AC07176B63FC2331F40AD8D06400CFA9CBF1EF7983F45A99BBD100ECEBDA24000C1C3D542C0A0FF5240737EA94036DBDD40C0375EC00C6DA2BECC09BB40257C63402EA90D40B7F228BE597DDFBFAD29A13E51E03DBEEB9386C0D8B055C0F15F9ABC0561A2C04572F33F1508CFC025CB9140F73EDFBF4080373D5D02EB3FC8D8ED3FD2B99CBD3673D63D7278863FA2DC613F82EC4D4032B427407972E33F726BD23D6E8B91BF1D2A9EBF728B3E40FA2CAD3FD9344CBF5D6A37C0CEF5E53E1C6A7840906678C091284C40A630ADBF246A82BEE1204DC08AF3FF3FAD021F3FB530ED403356EBBF3C91A2BDCAB40D419CD08B404C058540F7496D40D4BE50BFDAC7A0BFA294003FD03B183FD7BA5DC0F2E9A64045EFFB3F4E9934402F8B61BFD8FEEAC0847A81BFF53E5DBF54CE50C0553AC2C0916389BFE126D0BF5800673EB29F383F24FDBEC037B9F2BE6A9F353F6723A64043997D40D74D3EC0B33248C03FE77FC0EF51924030261C40212FFFBFFAAB02BE32AA933F461EFD3FC7049ABF00E948C028B6D8BEA15CE3BF74415D40DEC77BBEFEBAE2BF024C47C0DB03ABC0548C75C07F07D03F6DD837C08EFA8D40F42D54C0A3AB2A40A2D44A3C3CBB033FA7493140552C6CC0AB14F2C0647A87C0B451FA3F8BE2523E41B60FC0919835404CE4AEC03CF93F40EED380BF04B946C056FA743F180155C0523FC04030248ABF0E793740F4903340AC45B83FCB1518400BE000BE68F71D40A2DDC3BF0CF546400CF13CC0B07DB53F68A59BC0C097EC3E3B0DF8BFD7530AC053F16C3FE00C0940B0713DBE8CD404C0D3C94E402DB651BEE5F83B40A2C2093F8D3D79407F64B53FFBD233C08D365A3FD68B893F0B43D8BFDB0341C0EAC15F3D4D2D8ABEE5AE1F3F214BE43F7D958AC0BA0A69C01D376740205B943FCD496AC09E143BC0A869CA3F151804C0D67F2EBF6ED22CBE87046540222C8BC08EF8DF3F9813A53E23E4C040CE54ABC06093104050D2303F3AE939408A048FBF31AD60C099E619C06E70CDBDE53386BFBDC23AC0DC3B1BC05BBDAEC023910C40CC03CC407308E23F23CA32C0F79DF23E42B51340968D27C0FE6229C0FC8DA0BF4715A33D3B86063EB7A356C0FD3B8DC08B8894C0EC66813DD94687BF5002A94062F7F73EC03100BFF46A84BE14745F40EC388EC0D71002BE3E807F3F0E28664099A9F6BEB70719BC09842DC0D053E4BF1EC0F53FD7799C401C8472C022B6CBBEFE9149C040277240971882C070ADB7C0EC86993FDA2C35405CB5CC3F42C53740CFD8FCBFDE4B424026072DBF98878F408EEAB4BF4BE6913F141AADC08FDD9840C9E63740DA5F88C075AEBBBFDFB0B43F1711334008D74FC0DAC85240BA28AF3ED8B63BBF83BDB23F1C2A94BE47394FC0D991903F98841BBF78A27340C56D87C04A7B6C3F5419423F5F3486401FA7A1BF49B327C0E7B8193F8B2DEDBECAB1724051130EC0AF971A40DAD2CAC0C7CACA3ED0778FC077FBE43FB541B3BF2C79B740C0B73D4047D1FBBF3F2E953FE6DEB34022B49440B11BFD3F450673BF23D3D93F70618940F1B5EA3EA26F7F40150A36C070BD6CC0F7B3EEC00FE3993FA5E606BEA7C186C02452763FA05C75405BC682403CAF833FE184A9BE1681B1BFD73FA2BFEC2847BF0E644E40CDFABB3F81E939BF91F6023FA8A6B13FD90A3EBD5320F53F293367BFF76286BEAFC8FBBF81CFAD3FA62AC43CBF6A4DC0A6A830C0ECC6CB3F70F03EBF19800040D1DB52C04788BA3E4882C13FAD7EE5BE85E0E13F3C948DBF7A21EBBE7B44B2BFD9A11B3F0EEEC73F443C1FC06674DCBF8D7049BFAD0A6D3D4B617AC0B16703C104341AC0A94CBF4071BA3F4033C2C5BADCACC2BF3E7F2040D6017E3F7931BA3F07F994C06BD384C0F1758AC0B7E022BF505EDB40C196DBC04C5378C0D1D334C0886E8A3F9227943F34C7AD4058ECB5BFD61E0D3FA89B133F6E1296BF95F0ECBC48F1313F54F28940113ABCBFAA10D1C0B993A33F6BEA8CC003B15A3F9D646AC0F9E396C046AA333FFAC98F4090D4423FFAE48240636236C063C92B3F02027EC0"> : tensor<20x20xf32>
    return %0, %1 : tensor<20x20xf32>, tensor<20x20xf32>
  }
  func.func private @expected() -> tensor<20x20xf32> {
    %0 = stablehlo.constant dense<"0x2255E240441DDEBF9D3EB9BF4BC4F3BF8DF2E33F07022CC084333CC07621B33FB082893EFA81EF3FB6EDAEC01C098BBFF74109C00A7604BF2BBFC5C0F01BF63DA6F2E0407E9BA540B2763440B36C00C03265D7C06D7E8E40287229408424EC3EC639BE404233C0BF3268FDBF9F128F40F06275400F55DEC0283E51BF6925DDBF5779AEBF6CEF214009D916C000CEB8BC5A3DB5C0AC018ABF6019B7BFCB45EDBE32B9CF3F59620F416C1D12BF909344BF8F46CCC022AB1BC1363416401D8AD93E629450C0A8AAC9C09841A8C038E29CC0A9E3DD3F18C6F7BD9CC0A5407A9EDA4071B4B03FCAF8854089F79740067A1440BEEA0941F2DBCEBF3A899340908892C0C15EE23FE2CF96C0F7A22B40E7D3D83FBCF2074063FCBD3FDDE586C0D292AFC0D80E6A40F04EEBBE7C4B8B4021A50541D8F029BF00B2093D2FF4BD406028023FB9D70EBF69BCD6C09C74AD4040181BC0C69D2540EDD4B43FD7285640EDB432C062DE98402683C4C0BADE57C021B9803FC1572FC18D3EF2C099FD14C1B59181C0362AA7C0AA9F8A3ED2D14E3FE96F853FBEEDC83F5ED890C05890ACC05F2524C05430F9BFD4F836400A86C3BF78D1B1BF54D46B401A5A66407ACA67407A8F2FBF38B799C00AB363C0D3266B40F6470A3FA1BBB1407450BBBF59660CC1596A54400AF53040A0ED454072F7A9C053DD1CC0009E5C40462909BF035883C022CB9A3FA6D8BB3F4342A140E07F274091182C40D8FC16400FFB24C096E3683F35E9124068239C40EDEE9F40B6B620C068D353409AE5F4C0A5A8A3408A6930C06ABC03C00ABB93C04C1D33BFA9E807C0FE61A9404756B140EC6D0540F152A84027B049C040261CC158911F41D84DD33E1C3BA840C9341740D49B4FC010A5543E840CCAC01E072F40BC6700BF6F5A07C066C6F2C004212B3F0CDA73C0F5ADA5C082AAC74019B428C0D7A40B40601E05BD2FF5164124DF8BC0ED528FC05D281CC05C0F593E20AB7CC096C6ECBF4EDC0241C3C816C0956F6140C45B33C0AFC4B5C0943B103F3A32C5C09496813F2676F8BF2ECB593F726B32BFFED5D840576124BFB6D58EC01211014048FDECBF1056853EEF4C99402617CAC0028DD13FE2A86B40BCFD19BFBE5600BF12F00440676A603FFD931A401AC413C03630FF3F280031C0D62167BFD486E8C03041C53F7BFC12C0D8C500C0B71A20C07A835FBF1EA1C63F8E662F40D7A1AEBFF0E2D63FE2DD4C40167BE7405038274176E94FC0F29312C18D3F4BC07CF23D3F635634C0AB91CEC0EC56D34060791040029CCE3F9842A0C04671A03EACE76A3F12FFFC3FC942E140A0A0A740FE676DC0CBC4A3C0634793406040D33EF768D13F8FFBADC0F8648C401F5DBCBFA24DA7BF98CFCEBF07CC99C0DAAFD6BFA27A6340B8E214BFE0764D3E89D2F5C05EAA30407B77A13F88D0AF3E147D4EC0C0B9EEBD1C62983F27E010405D3F96C04042DABE487573C0C4E75340CED10EC0C49485C092F1A2C0E428DABFE42136C0F86EFB40E97591C0F5A799BF8FAF7B40DF72293F0806093FCDC4E1BFA86FB93E25CF42C0223D0BBF12EC50C010371D40B6D4D4BFE28B084086D5A1BFB1678D3F2D860CC063AF9E40F4AAA5C0E007CABF5CC7C0BF682304C067AE0B414D1A36BF5FC9F7C09007223FFB14A7403E3567C0FECDB7408E585E40DC3E18415CF3ED3E8068A83E2EC095C09A723AC04DE80041E144B240C62297C0AE1100C126670BC175846840360ED7BF7DF80EC00B7D76C0C6485DC070EEFC3F76297F4058E22040F474424066C1C3BFE80746BF8C013DC03E1FB1C0489831C08A253FC056B526402C2967BF2C2506C0C2DA91C0DC0BD9BF823BCAC004A600C0D40D37C008C0593FD5D1853E8F999F3FFA8E263F00C3364059EA094080B7E33CCB9F903FD4C2AC4052D338401CE67FC0DDB31DC064D9B0BEAA92C640B00A16BF4C793BBFEEBD2740302442C00C526D405E403BC0041DC6BE72059AC094D7BE3F863400C032575840F68EC63F4FDB513EB412AA4067F625415CD56240268F93C0801010BDF963A7C06F121A40FC49CDC0B4C2BEBF4E2690C0D2F3CF401EF7BD407C4AD83F8658993FEC1E0DC00ECE8240237804412C1E174080F523BDB86B9E405CBE4DC09A60F4BF588CE63E7C23613EB24C0B403FE62340E4C9DBBFD505AFC0F7F0B1BF08DE92BFAB1634C0A0E941BE281C05C016C99240FA43E63F0C3746BF8AB90CC1F9DE91C0D57ABFC07CF81B3F02008C40B0BAA040"> : tensor<20x20xf32>
    return %0 : tensor<20x20xf32>
  }
}
