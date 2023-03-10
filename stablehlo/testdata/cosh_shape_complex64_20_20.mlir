// RUN: stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt -inline | stablehlo-translate --interpret
// RUN: diff <(stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt) <(stablehlo-opt %s)
// RUN: diff <(stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt) <(stablehlo-opt %s)

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0 = call @inputs() : () -> tensor<20x20xcomplex<f32>>
    %1 = call @expected() : () -> tensor<20x20xcomplex<f32>>
    %2 = stablehlo.constant dense<(5.000000e-01,0.000000e+00)> : tensor<20x20xcomplex<f32>>
    %3 = stablehlo.log %2 : tensor<20x20xcomplex<f32>>
    %4 = stablehlo.add %0, %3 : tensor<20x20xcomplex<f32>>
    %5 = stablehlo.exponential %4 : tensor<20x20xcomplex<f32>>
    %6 = stablehlo.subtract %3, %0 : tensor<20x20xcomplex<f32>>
    %7 = stablehlo.exponential %6 : tensor<20x20xcomplex<f32>>
    %8 = stablehlo.add %5, %7 : tensor<20x20xcomplex<f32>>
    %9 = stablehlo.custom_call @check.eq(%8, %1) : (tensor<20x20xcomplex<f32>>, tensor<20x20xcomplex<f32>>) -> tensor<i1>
    return %9 : tensor<i1>
  }
  func.func private @inputs() -> tensor<20x20xcomplex<f32>> {
    %0 = stablehlo.constant dense<"0x519220C01EA348C0544592C0F0D9C4BFD0BD5DBFE49F1F3FD9AFA43F4A5E55C0F4063B3F458A774000B4AB3F0DABA63F29177DBF26C206BE2E95D9C00DBBCEBF754F9DC04BB6F9BE1B1864BF3E4075BE6D57943F166809C0CA8C1E40BAFB8EC02882DF3F99665F405B945C40084A0BBEF1F095C04FF13A402D47A83FBEB3983F3B3B933E43AEBB3FA75842BFD82E71C0272F2F3EC762E03F1D9A6F403F9D4A400D10B840D5AC5CC07C6C1040AE489A3F6F99D93D5D9F7CBFAD0C3AC0EA5E3BBFF15C0C4020BFFA3E815D7E3F55B59A4060777DBF36EA814021EA4B40FAAEB3C05822B43E929C1DC09B220E3FCB884A40228FF73FFCCE0C407A65853FD82FE33FCEB8FEBD625746C0037F1CC0C8E7F1BF74FE8AC0AF830EC08C221F3DDE927ABD1D3FFB4081311CC025FFA53FB287C73F673A9E4047C229C00439BB400E1D4FBEF97558BFCC0F83BF32530B3F5A84C5C057668F3F73DD403F140F84401DC70840E30A7FC075F0BFBF5D25A13F31E71441AF637D40F0048740667CB83E24999FC0ED714940E28B09C0A743CFC0D88B67BF7E0D1E409AE9CAC05CA8B83E0CED953F4DF3AA3FDC8B1EC0B02C393FFD8639C0B2258E40AE4C963F109328C0265C95BE6FA018402411C73F399B42C089CD9AC0E0AEF5BF598107C0EBDE413E634DBD3FC64D963F0D72283F2091703F694353C0C6FB23C0B93ECEBF30E8AEC0A77707C06402E5BF6F583E3FD17EBEBF4F02763FD6B728C0C69981C06B83D6BFE84B5DC092BEBFBFCB233BBDED3E3EC074D395C088BDA03F97359BBFD22C85402C5C20C065F163406E6CB5C058BAE5401FF7823E2FB560C0CB79893FC56E2040C3B63840F27B463F650E533FA7EBC5BFFCA283BF4712AF3FFB03CB3F54D58BBF419A32C0607D95402A21A83FC9DC29401BF2B0BEC26CC6BFDCF398C02338F1C06BB6B93FAC371640CD22343E0A1097C08BD3063F5BF8A540E1185ABE26A69FC082938B40081580BF4E880BC0DF17543FF6171DBF4F21C0C0A2134FC0A0C02440CB9BEBBF39CE4D3EFB5E7B3F2279BE40970D1B4004BF93BED37C5FBFB02A753FA69558BF24554E3F2B2F6A401DDB5140B6300DC0F34D87C0198AC1BE79289040D1E5383F99C65D40EFE0CFBF95FEA7408824EA3F8CE3EC3F084DA8BFB2E96B404C80BAC0F1C132BF3BAA823F09357140D6599F3FF762F5BFCE274DBF065EA9BF74CB94BF3E215E4024006AC03CB02E4005C6CBBE776F96BD538E48C04A887EBFB4CDB2BEB9C9D4BFB85D8EC0E811E3BF547CABBFF45EAABF4BBE77C02550AE3FBC93BCBF9FC90AC00F14A040002272BEC43DCBC01C8E5EC0E908423F88438F3FF3A843C0A9984A4014B2034094ECBCBCE531DF3E38A9F4BF64152840B5446E4077630840B1296FC04BA143405CA44140342254405AB826C0A3CC42C0F2101DC0EB54CAC0A9793DBD593603401A56E33F14253AC0DFB65F4091678E408EC6E83F98960440AC4F0340822D2BC0469BDBBE22D763BF2DDB41BFA6EF13C0F8451D407DB519BF29E317C060E386C0084347BF33EB25C08692D83F687673BFAA5FFCBF9BB316C0599FA14048F07BC02F473DC09EA2DE3F6AD8F5BF5A6D0DBF1213963F1FB9323F7A294540577E703EE7497CC05DB61FBF0CC509BFF63BC9C0F6FDED3E2BB6943F5BD7644092069840DA42A6C0F185C540B1525FC02CDDF13EAC371D3FF1EA9E40C5A780C04B2257BF9C1938C027FA244091A7A83F6C4BB1402C019FBF5FF5204071274EC00C8164400D288C3FE06497402283A5BC0ACC38BC04A33D3FB69B61C016D5CFBF58837E3F93B2E33FCC0E54C0C554B33F070B30BF3C2914BF2CE5C03E9397F53DF89905BF27B8263FB5EBCF3E2E2089BD041A8040DEB736BE333D25BFC2FD043F3BAE8D3EDF2BC4408CBA95C0C70D4CC0515795BFA40C26BD05C143C071D900C0963692BFAE6127BF4E5DA0BFDF480C40AC4C16C011FAE8BF6CE0EDBE0EA28CC035EF743FDB7CC5BF44EA103F867F33409CF22EBF549C843ED0279EC047C4A33F5D292540BBFC2440700C5840F2AF87C044C78D3EA15637409F34D5C0BCD664C07CAE53BF3321E03F8CC84DC09D737DBF100EF3C058DEC34069F190C07946543DFD1583BF232252C0A04036C0C72CC03E2A299DC0538CA5BF9DE41CC0E68A984086883FC0649EAA403741B5C0336207BEB1BEE63F4795693F253B3640DA3FEDBFF654A8BE1E42124009905C40FCFB363F5965C2BF7000B0BF46A262C0FC490540A9A089C010B1503F8D4A4AC056A7A1C0AA61DBBFFDEC7E3F5876FE3EC89A97BFB14C4D40CAD488C0A459A0BFD8CB4FC0F3B70DBFB2824BC09BF8C53F80CDE53F4DF8C1BE3E32B7BE7EBE87BFA64A90BFDCD5A2C02C46CD3F8E7E01C0CF3C4D3DF42B61BF9FD526405BB650C0A42350C0818F063F81D4E0BFC43C5A4085D4C7BE362A8B40BCB6EB4094E868BF14A1A840E65B56C03BB5DC3F517C48BFA609D6BF28D414C093E7C7409E1B9740368B99C093DCE240B6EA834004017A402CACE2BF7B0145C0A8AA393F12186B3F6A5ACF3F0B9367C0FC12EB3F310A06C1223D5740FC7DF63F95893EC0E6F8B4BFCC9003BFA526A93F9EB485BF60BD323F71011AC084D15C407060B34035413EBFACB50F40ABCB99404ACCDEBF68452B3F6953F63FD262403FF3EF70BFA7712C405B0EF43D1012AAC067B8813F98E05FC0056D89BF4E5C5440024791BFF5CCA9C0D5A87DBFF202CDBF6ACFC8BFBC07473E17CE75C0D1114DC04E8A4DC010A26EC001B23340A20F2EC09659423F64EC0BBD2A2647404062D240F187CE3F518E23C0EC2DD7BF32ADB23F73A767C0F499F73FFC0222402B2BCC3FCDD771C00B43834078444E3E5F8D40C006219BBFDE9FD1BF76B508C08C03C2BF5B0A4F40C4DD51402789223E52E137C0B9D11940FC0E20404557ABBD5B2D7ABFF85974406B974A40DFCDEC3F6A87124095B9F43F15089CC0416935408007DEBF53BA0DC00F1A193F96AD69BF77871940ECBDD13FC41DC23F9CD39E3D579DE6BF5ABBF93F70B2363F184E0A401A60A5BF7B6BC33E84A5933F177EA53FE1D4C5405314153E58483AC0F44A7CC0DCF525C022FF85C09E1202408E3D44BFCC9565400E1B8840E5B15640D296E340C9540A40EDD91E40C1DEE0BF287ACC3F1FB01940D1851FC0DB2524406F6F6D404D6007409BD17DC0EA072CBF4FAAC4BF0A87C2C07587923EC4AE7EBECBE74C3E1F7F9EC025B200C0AC1AE34015576B404C59C2BE6668D9BFC467DABDA7EE64C05D7F91C0987F2FBFBF436240F63334C04B940640AD43453D3A27CF3F029B00C0A033B43E5C513840D1353E403D6906C0EC58E03F731A674041088EC0186AEB3F51F40D409EA007401704E8C01604A04088BD7D40F22DA2409041AEBFB8497C3E22D71C408359E53EDED462C0ACE0F8BFA3B09CBF79D452402C949BC0B492003F808289C0B62C57402C07C0C026CF70BF5A321CBE97F6B73F80C79E4032AC46BFFFEFACBF1EA1F93FF81F0CBE3E9C88C0618019C018237F401BCBF8BFF2163CC0CADF76C05C36BCBFD195C7C068A19440A06F71C0E26CA93F410659BF165477C03371FDC0BB737EBF8783A2BF789F3BC0103BE03F376CC73E13E11640ABA4303F530C24C0B178C9C0758965C07D0B84C05909B6C0F54F1FC0D18C86BFC1CC00C07A3E9740A95056BF9BC83F406385FCBFFA9CE4BFD6F53E4000ED243F6D244240BC3023BF300674BF1582874000A88DC0C877EBBF22678AC05CC65A4090F5CABF89A82E401C8876407B0EC5C0B54F61BFC359C3BFB2EEA53E8B1E3C40C9E2AE3F24911D3EC77161C0567B56C009E8A4BC3DA992C0A0C2C93F904C1A40965BB63FCE62993F365B2A3F56A1773FEE53FA3F254547BF35FDE93F949B533F89B687BF503704C07B26E2BF15C70ABFE8AFB0C0176ADCBFCBDAFFBF686B9FC0C0FCCCBF857B74BFC19A823F1ED5F3BEC5B96C4002CFEDBF1074E6BF0EB5AEBFE6A73640CD8CAD3F24E7E5BE0C9BD2C011A98FBF1832FFBEB10A023F6D51BA40268DB1BEB087134048A7DFC038D2B9BFBBF07440CA925840FF45DBC01C8D26BF5240963F351218C03D953B3F90E5523FC7E46FBE53F54FBFBF9653BF93FCACBF3BF02040FCF513C024F79B406ED39C40DB1F96C099E994C0C3AB0E4084EC39C0E54E0BBFBDAD2E4048EA67403446563F84DC834097179D3FCE7786BFE7819CBEAB7A4340D3834D3E8B5BBC4044661F3D7CE481C0EF2E8EBF7864CDC0F8C755C02BCE6C40D7A535407743CEBFB7F694C00BB68ABF88E392C08DFBB6C0F42E223F22B839402CF970BFDF0E11402D6EA8C03BF4D2BF4A8AB3BEBCE2D9C0E0C2DA3FFD688940987FA74030718C3EE24537C078CD9740B8A5C7400ED980C064328D3FA95FC0C0A8B44640527427C017D3EDBFBA87FFBF062983BFF82D9D3E6BD58C3F7047493F2BC9BF3FF09957C0B776894032FC0F40B1181EC032DA8240C3D916409529733F19F179BF12DEECC039716FC0"> : tensor<20x20xcomplex<f32>>
    return %0 : tensor<20x20xcomplex<f32>>
  }
  func.func private @expected() -> tensor<20x20xcomplex<f32>> {
    %0 = stablehlo.constant dense<"0x3CF7C5C0D2F6253D8E6FCB3F232641429D64913FF84812BF30CBF4BFEB9CA33E60CD74BFB88A07BFEBD80A3F15DADB3FCD1FC23FAC901B3E12EA9EC1D91EE043801771429FC0FF417D0EB13F913A763E851A74BFD82A9ABFD68EB9BF1399B7401FA031C0BE4F73BFB51179416F1508C03E8753C28DB83DC1247F3C3F0086CD3F589BDE3D3974943EE3FE86BFA48FFABE57483CBE21202D3E2713A9C1791B03BF411216C3EBF03D42A7D6DC3FFC278D40DDF10D3FD7E0B5BD487DDA40FB1DC3408A228040363F0540CCBF3F3EE80D94BF07126EBF86F16B3FC03518412068EF402BB153BFD9EC66BEA53394BFA82E5CBC7A0405C03F362F40826BA5BE4D979B3FF2DF80BFAAA4AD3B1D4CE9BF2BE7AD404DD9BBC1D0F8F341C7B67F3F31B41BBBD86E75C4D32C4FC410B3C03C6595D83F1DEC77C22FF103C242332A43DC9A0BC26E9E373F22B94F3F7D86923F4B65813DD8569E3FC5E56F3F0C1085C16629D1410438F53F7F87D64140DCF1BF0DB6433E708A46C111A0B8C1392F943E0F6BB53EBCFDCBC0B8A81BC196E34843E97D7F43341DBE403E9AADBE3F3DD43E37D4AD3E32D7CCBF01A48BBFBE319EBFDAF641BEDF468341A8B21C428A8CD640960FFF3F3EBCAE3DD736AC408893A73F75AC25C17C9AE7BF155C36406344BF3D6735423E2480B33FFE15653FEE67BABFE739303E8B2887BED508CE40008C75C2C61ACA4294E41040EE01FCBFF1A0AA3FDE88DCBF47128AC07C3AAFC08F3528C01C374DBF262016406DBAC63D671898BED9DB1BC127552A3F9857C1BF4196CEC1A37298C19E876641F4212241609D1E447BEC2543AA8DFF4015486BC10923BFC0D204C53FD1BA643FE4C3203F1C1AA23F469DF53F81B002BDE2F7EA3FEF58C7BF38E0E83E258E5941A9AC4E426002D740274D19C0A2C2293ECEAD0FC030B2E042C41869C45537A6408213683FC516424237ABE1C14DD1AE42FA3797C12FB3C9C131DB8942F63662BF6BE7763FC8A98E3F109808BFC9A449C3070D98C13436E1BF652BC9C03A12113F874B2C3EE2B910C3094CFD4265622B3F4594653ED8AD7D3FDFEA54BFDC5D95BFEEFEE2BE4E6FFCC01AB62AC13F13FF410E7F4A41A6C40742022FEF4160235ABF1F437FC1D2C9C2C14D33B8425552533F332840C0B26A8F416C290C411AC5273F5A1625BFE10FDE40331DA44122AF1A40C8F3184024684C3FDFF0CC3F212E60C1ED0EFD409800E34071423DC06A5780BF6A5E183AE0C8B83FA656CC3E500D36BFB6051DC0D8C1313FD75132401D36C1BF452996BF9B644F3E134EE8BFB32DA23FFB5484403F4A833F6D0D853CDE483C41C3E131C1F420D8BFC432ECBD85C5B1C074472741CA1E683FAE8F1FBC176C40C0429ED0BF9D0C30C1F0268C41605DA7C1A690E3BF2CC822C1307DE2BF21A4D8C04749283FE669BB40BF9B6A3EC268ECBE2F2D28BDA72B3DC0FE9E29BFA3E088C0F47D7EC18465C2BFF56E2840730562C0DE7ADCBFEE2D303F59EEAF3E9DB160BF7A221D3F0F409B40808551C0863425C0309E95C0F5F58FBF6EA6E53E7CB5D03F78A608C0F56D25C05A9C1F40BBB15AC2B2E45E42E035CFBFE26617C177EA3D406051E03FE987AD3F9346703FD7C12941F2C921401654A74172B2704137FC923F4531473B0402E23EF34FE23EA3B62F3F38A78EC1F063B342EC571F4127A569418654EEC04890993EF3ED21BFA0C5944192FDA54199D8F0C0445297C0F3AEBC3F120D96BF8F82C2BF7D486EBFAB9936C19C6BA640F337FE3C94DCA9BF9B04803F24F76E39F2C898BF06449B3E77D7B73F5DC402C0981340C051DBFB3ECEEFD43F8CE59ABFEB848B3F1E6966BE09855F3FBC7275BD836B8F3F0F3A8D3E2B1727BF2547503D1BC34F3F0A01DD3D601F8C3FA9F5173EF152F5C0B5AB65438ECB9840610F324169547FBF27275C3B0CACCA3FD60E56409A82C33EA1902A3FF0804BC0449849C0E7383540255DAC3F1EC4BA41C06E04C27C1E0440D73B99BF73BBCD401B4DA6C0104B713E6291823EDFDFD1BF7ED3613FC325CEC0D471C2BFE8950542CBCD17C19EC80241E9484FC005A44141FF145241625F3DC02FD5523EFE73C83E96888F3F30A125C220D65F435943053FE96A35BDE8744CC1A8D4764022BD583E5CD7C03E146FC1BF1156893F888968C2547F0BC17873A8422CFC6F42A0666DBED32404BE0C21B1BFEF139B3E660946408AA5803FB09097C0CCC1BABF9205873DF4B246BFC118F8BFFE5938BFA4A6D0BF6AB9674061E8ACBFD9F28E3C9E5632C118AE9A42BE10AD3F38A70E3F5747E4BF89BDC83D400A344118A80842C8362F41BAEDD74067DE943EF3F43FC139F6374096998ABF1321053F3B4AA33E4A6B203FF07BA4BFD6C090BF274809C02966233F03321EBDE782D8C017CA4D3FA3203341FE2FCFC0BF0938C0EAB83E3F4413C4BE4B8DBF3E7AB4F2430B0A1CC4002DBEC2AB5AA041063C03403030F5BF7FA3F1BF2F75EF3FFBD720406D1C81C374F02742E1102FC21D27B2C1E4D7AAC10A1741C09306393E4F21463FA5CE203F664915C041968E3F0ADBCDBFFF7A29C05AC4A0C0F35E584102E6C43F90B81A4101618F3E6E2F05BF7F959C3F40EA4CBFF879AAC069F2D53FB439C8429BFDB7C277D0E43E4EC494C0B5801340BA53DBBFD8992340647E1240FE6AAABF2E84F0BE8611123FB495C93DE901BBBF2AB6D53E04E3CDBF5160683E3BDC753F22F893BFD5A441BD338E943F9D471D405516E3BE22F4B9C1C161BABF4D9225C18597DBC024CAF2C0A55D58C06D98A63FB569E9BC16722C412B014E40AB280BC033A7AABFE88CF73E7A6523C06285D4C0176E8BC13F321DBEF7CFC740D1CC48C1915C8F41EA7381BFE63EDCBCCC5CFABD178EC33F4BE3713EB55D8540E3A449C13CA3DEBF1AF279BF3D2D2DBD280A8FC07AD75140ADAE0F3F8C338E3D2F16B6C1AA250BBFB05009C0A16415404422103F98F45040E546B2BF27E505C1320C7540FE0223C08D6088BFECE834BF5559143E65491E4016086BBE6EC49ABD1AA52D4033681040C5E89A3F96BE83C045D0DE3ED604B73E3177F93F54D42DBE9AD97BBFCAF708BD3CF0AFC1DDD45641FEAD6AC179C8EBC12F2097BFB53FBA3EDD8509C2A7DEEDC0AACBAAC3B8BDFE43709C8EBF0FD6BAC0AA04F3BF5E85CC3F0548A3C0899151C0315F29C15BB18B41103EA541745B83418C6318400220E6BE422D813F214192BD50EF783E1D55483EE2172740EAA42AC04DFA9241F23CEAC035BB33403AF38F3E30D13CC0A2018DC16A1593BF7C79913ED90F88C07E81E5C0F31243BD071E453DA0176440ED9DA1BF87E80CC10AC7BF3F40F93FBF54327DC02C3FA0C0D1638E41B1ECF8BF4A8C1C40B45A1940172958C0EDEF49C2B3BF59C2BE1E844170659BC2D14C4BBFC768223EFBC681BF0BD1393EB9159B3FBB0A4E40AC9C004013F454419C21E9BE8350F53EBCED5D41526F8040B2BCBA3F1DEB283E1C840C3F994CF6BFCFEB923E35F4553F31606340748BF0BEB181D2C11E59C141650D1DC12497C8C1F1C1E4C066D6C5C0566E124018ADC2BDB50828C2156CF541FA5EAA3F4C84A7BF079BC9BF1438BE41C146E93E8A848E3F1BBAD8BFEC3013C13F2D43BF2E6B903E60BA85BFF1CFD0BEF7CA74C3C06FE9C2EA58CD4155C68AC175D340402B3BA64078275A3D941F6B40D6B1ADBFB1D30ABE126548BFD5735C407875FD40396ABD4012D505413851C5C01E5B2FBF500A7B3F39C431C1C74921420B6B11C2312B2541522115C0C50E70BF58F8BA41FC083C402A13813DFB4A7F3FD20884BF6FA8873D630C0440C6D48F3E1FA284C16F6A61C080FD03BED08AA3BC6063F0BFADC1C53F33D24C3FE06EE93F7DA8323F2CD9163F764924405FA51BC0EC580A40DFAC0E404BA344BF72158F3F221A2540DE97BB3FC29E96C1A825F742ED0F803F977A5FC00FD1BE3F659DF83F084BB23F93A20DBF6025B7C089E49AC1CCB3223F3367384008B7ED3FFA3507417DE4863F07B20B3E33FDBE3F870F283FFBC3813F9E5971BEAFF735BF4F7286BEF1DC804274A40644BC72B2C1224DB0C00033BC43BB3E8F436F86A3BF8CB781BF08C15E3F124A163F87EE343F39BC2F3EAEAB973E68D9663F1B6886C0BEF790C00A0F44416F8280C21F424DC0E59C59C2C33792C080C089BFEF1287BFD9716ABE50EA48416A7C5E41CF042641E8F1E741C0D9C33FCE53C13E18A026414DFD0640B8E03343EF1CE040DBC74D41908DCF41A14196C33F1672C274829AC19A36C1400AAE18BE17AE19C02B6E4DBEB840A6BF6C4CF542172DB4C27708AC4032C7EAC0645A23403513824039F72140010E5C3FFDBA79C21050E0C3F42192412DFDFDC1757E7FBF13159CBD3C8A65420E6F22C090444A41171CC8C155F24BC32361F0C0DB78F9BF17E8D040EB38F93FA89A45403625F33E2E4F8E3E6F88C43D40155E3F8B47BCC0BC365441F54E70C01DB63AC0A003A9C12EA2A841DF10553FD12269BFDA2B29C4E951E7C3"> : tensor<20x20xcomplex<f32>>
    return %0 : tensor<20x20xcomplex<f32>>
  }
}

