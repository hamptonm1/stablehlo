// RUN: stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt -inline | stablehlo-translate --interpret
// RUN: diff <(stablehlo-translate --deserialize %s.0_9_0.bc | stablehlo-opt) <(stablehlo-opt %s)
// RUN: diff <(stablehlo-translate --serialize --target=current %s | stablehlo-translate --deserialize | stablehlo-opt) <(stablehlo-opt %s)

module @jit_testcase {
  func.func public @main() -> tensor<i1> {
    %0 = call @inputs() : () -> tensor<20x30xf32>
    %1 = call @expected() : () -> tensor<20x30xf32>
    %2 = stablehlo.multiply %0, %0 : tensor<20x30xf32>
    %3 = stablehlo.multiply %0, %2 : tensor<20x30xf32>
    %4 = stablehlo.constant dense<1.000000e+00> : tensor<f32>
    %5 = stablehlo.broadcast_in_dim %4, dims = [] : (tensor<f32>) -> tensor<20x30xf32>
    %6 = stablehlo.divide %5, %3 : tensor<20x30xf32>
    %7 = stablehlo.custom_call @check.eq(%6, %1) : (tensor<20x30xf32>, tensor<20x30xf32>) -> tensor<i1>
    return %7 : tensor<i1>
  }
  func.func private @inputs() -> tensor<20x30xf32> {
    %0 = stablehlo.constant dense<"0xA18E923F2060F43FADAEBCC06BF930C04C233DC0E9378A3F55CD31408AD0583F0D5AABBFF827B43FB1B439C0075FC440CC71C63EFE6E9C40B6240641FA602C405793BF3EE3E5BF40C6A2CABF68BB44BD0BD7A1BD920856C08D24B4C06CA6983F3D1672C0F0725BC07FA6BEC0D1CDDA407780CBBF8CB0334098430EC0FE29B6BF53A2A93EC04DC2C01F2ACAC0535624406F298EC056FABB409FAE45C0FBAC8EC0DE495140A788FFC099F82E40B78AA8C0555DE1BF576158C053656D4042A3DBBFC044CABCD6CC14C0DD0BC13E5D1B55C012B8F63EE29991BEBCA308C0B70B433F25018A3E0D5B01BFAAC7C23F785FACC0718C59C058A2C4BFF6911040F4AFE8BF3BD8903F6E151FC0D7BA473F51940DC06C0FD6404142E9BF5A5CBD40B2B2D8C0ADC2BBBF180386BE707C52406F740C40D51A1D40502509C0292FD83E233F5D40C066E23FD81FB0BF6A4E023E729BC33EE42314BEFB1C143E58E544BFA08B244019B5843F8E528BC02179A040061D8EC0ECADCF40F3BA8D3F779086BE7D0EBFBF253AB3C01C063F40FB69FE3E3A84423FF3A60EC06F14E23F5ABA01400DF523BE781DA740520E703F2F2BC8BFBA4F4CBEA1465BC0BAB589BD69483BBF1FB20EBF659FDF3FB4B53AC0512743C0CD21EFC010130BBF61F4543F109E36C0343BBBC001C736BFB7E805BF3D719F40403787BEA3C8EAC02D911DBF84FB1640AF3D0440733A9B3E3AB5ABC0206961BF43DCE3BF03043B40E10FD63F761457C0EC4628C0D9709EBF3C2499BF97E2FC3EBEFF3840C539BB40D0BE5F3F11ACB0BFF5D220407BB0E13FC58F4840A64A3FC00D5A144082FD003F09A147C02F872EC010CB9640C3A1E7BD83FD1DBF82603D3FB812E4BF05E06CC0413D044049721240D60F024010A686BE2F1196BEA4F67B4063B206BE8E02633F827A81C0540B33BF85514340872EFDBF46EEE9BF2E119A3E9E461EC003E6C54094695F3F939628BE301F9EBF1A1DC0BF67D996BFED355940472951C09944F43F4979204077EB174062829F40A331D33F4CC0BBBF37BB0E4009FAA83EA60B3A407FA9373E47EA8CBDE47CE4BFA31FA4402E6A0AC0EEC91F4069176540E2A46B3F0CD516C04DCA65C0B5069F3F42AF52BF469E9FBF1F11B33F87CA743F506D7BC074B2C1C0F027283F300B9C40D78D3CC0FC4A0440DE698FBF543216C008C7A8BF922EF93E56A484C04B6AF03F591B56C0269F663FBC74AD3FFD56133F3AF410C0B57098C043FF45408AD431BCDF0368BFF17BE2BFB87651C01C8F603E51EAEF3F794F1EBFB4C4AF4034AC3C400D5D6740C1C602BDEB677C407F0285C0B9E6883F9F116740B7D2E0C003D299BF31D51940BD2F513F21672740FD3A9A3FD3060F40839455C007A420C06946B1BF046C72BFEFCCB7C0D2761EC030C618C0E8EFA0C03D374DBF4AB3A5C0D8B3713E291C89C081FE50BEEA3E1440BB4A2D40D39010BFB0C822401753BEBF40DBD240631B45C0583C5240DBE11FC0D582FCC0E20C7D3FB43110C0D61AA2BF9FB433400CD28ABFE75B9D3E51BB9E3F8793BE3FD87483BF2E35B640EB02ADBF7017D2407BDB93C0C253FC3F4DE312C016D1C63F0DF994C0815AB8C0F7E43A40F8FB003F7D86A4BF848954C03C98A1BE337FEB4026392040403D27BF7649333F061E7BBF1FB9CB3F40280640E63EAC3F35C5AD3FA8B19940F15796C02789AC3F1163FDBEC32AE4BFFBC04CBF12A18FBF0BE9D3401C237D3F50E2B9BFC061C23F03D679C0652C5240C6459F4076277D4025B6C53E6F174BC0F224A23F08A9A240C1E85A40F21F244026E52A40C9FDCEBF878D7CC0E5F3953F6F7B82408D37AABFD625AEBFCF7204C0878710BE531715410E97AA40270FFCBE25BFCFC02E3EA8C0C81D9240CC8F9D3F165FABBF13ABF0BFDE7D7340578D1C3FFDE301C0D0071F40F77B5640D41A533FB063843D7590BD3EE1D26CC01CA5063FEC7653BFF8F0D9BD0EECA8BFC60DB340E765B53F04D727C0AAB5DFC069800CC09C3352C06D7240C0BD2702C08866C2C03E50824066D65EBFB148C33FB2EC8DC0731EA3C0EE137CC0BEC306C1EE8F70BE71C373C05AB246C0F5B375BF26483DC011217340F7E69A3F793EDFBF7218C53FF3D083C043E10AC0077744C0849102407F732CC037B797BF061501C0127D1CBD3E4FC7BD420C01BF4B3A4E40188E8A405A5EDF3FDC2D85400E8880C04B05E63F3CD04CBFCE6AAD4052FB91BF945C36C0E837433FB651B83F9DCAA24006B40D3F1B3D0640E00BC13F7E044FC02DE96040D3E8F53E382DD0BDD9288FBF78BD05C0AC591EBED20403C07661A9C0E897C03FB984DA3FC8ED56BF38BA2DC0013D6040277C54C01F890B4067BA82BFEEFF594026E32DBFD76E17C09BA04E404698D73F9577D83DCE3A79BE385EC93DE2AC25BF56BFD3BD1FFE8FBFB7CE7E3F096D973FF2150340F064DFBF2FC7733F66ECF3BF5729D7BFF5CFCF40A80F704010720CC06AB4E03F99A88A4041D712403E15003F40EE5FC04170FF3DC83431C00FD2E140CF55C1C0F65C11BF0F710EC0411C643D8140F73F28FC5EC0BDAB36BFF1DA703FB808E83C26F174BF089D13C083D3D5BEC7B8B9C0E49621BE33ABF53D873805BF29308540993684BEC272D4BF2C3A9F3F5542DFBEE8F005C0AD20D7BE1673AF3FF6C0A43F82F79140EFD854BD3BB75BBD6CA0B840A82CE03FEE76A5403E736FBE36A92FBF74673540BF2560C0AAF319BFD73F60401064A73F28F2E9BFE90145C062982E3F69A5BDBF38708AC0F20BDBBE2D105ABD508385C0768A434095BC564004D79340C3B428BE2DEF7C3F91A81B408E9035BF800302C0B58E59BF784EAA3EBF6B2ABF749AB7BF311149BE33DF1C3F2758D1BFEF56D23F571C48C04A513E3CA147C0BFAC3D48400ED3503F3BDD45BF1FF549C05CD505BE43622BBF5293B13EF96C98C07E7ED2408DEE803FF7DA423FAD81C0C035660C40C06558BFE93BCBBF6F702F3FF18179BEDD23F1BFFE6843C0DBC38D40BFBED3BF6D378840F2D033C0A1A7583FFE8D913FAFE889C0708CC3BEE3184C4073BA7F40C3DC8ABFE99F34C0FDED0FC0665FCCBFD404A0C0CA9B4DC047A46A40EC8790C0FC9FB2402FEBABBFD0F723C09D498DBF66A0A23E056EF63FF27A25C07AC84C400E1BAFBF45E409C0C8DF83406AFB3340FAE4DEC09BC449C09CB3843E382377C0512AF73CA9EFBFBFA19C734095CA9DBD94963CBFFCD6163E00B568BF921816BF1D52E13E9663C93F2158BDC04A0579BF1F567CC0D56367405880043ECCE91ABEBA45FA3F80097A40E29BED3FEA2B793F5C97644095476C3D58A4A9BE3EAF9BC06F00CABE233E15C0C19D00C0A3E9B0400ADF9D3CB94440C025AC153FDDE601C0E7A7E33F"> : tensor<20x30xf32>
    return %0 : tensor<20x30xf32>
  }
  func.func private @expected() -> tensor<20x30xf32> {
    %0 = stablehlo.constant dense<"0x848C2A3F1D26133EFFD89FBBBCB741BD06B21EBDFB504B3F4E063F3D2BB3D23F666BD5BE44A2B73E5AA827BDFCCB8D3B106689411E3E0C3CF469DE3AA3A0513DDAB698411DF2973B470C81BE9B040DC6D84DFDC43F05DBBCB8ACB7BB5CEE163F9D5C97BCDC33CBBCC5F29ABB42014D3BA5D07EBE5911393DA075BABDD3A0B1BEF1F7DB411B5F92BBDDF381BB6EEE713DA2DC3ABCC7A6A13B9AFE0ABDA2D938BCEB41EA3CADB300BB9A72483DD545E0BB089E3BBEA9F8D3BC6283A03CDDAD4ABE91C081C7BAF3A2BD12409541CBE3DDBCD0FE0E4139EE2DC22072D2BD10B5104069434C420909F8C0F04C913E23A6D1BB0C93D0BC8D3A8DBE97ADB13DAE742ABE7DAB303F945985BD5AC30640802DBDBD39F05A3BBC3429BEBF229E3B440A53BBB436A2BE81115FC22748E63C14C2C13DE4738A3DD31FD0BD668C5441224AC63C320D393E2189C4BE71A5F243FC768F41BA23A5C3D93AA54392AA0CC0A003713D5BB6653F5B8546BC7FEA013C9B0D3BBCBDBE6F3BF8923C3F54555CC24BF699BE1481BABB8F0A1A3DA768024144E4114016F1B8BD9ED7393EF8E7F53D089E73C3ED10E63B6B3C9B3FF2E085BE3ACCFBC223AFCBBCBB934DC5377523C0A9C5B8C03609403EB5F724BDB27710BDD30A1DBBBC95C7C0BE5DDE3FF94E30BDD097A3BB99D82FC06F95DFC0BE73043CEB2959C22CEE25BBD73C89C0D6FC9B3DDA26E83DE5880F422118D4BB9780BBBFFF8435BED128243DD1EE5A3ED3D6D7BC505561BDD3FA06BF907B15BFD3C904413196293D939BA33B5BB8BF3F9DB6C2BE4811813DF7CE3A3EE917053D366519BD1E6FA43D3026FA40A4F706BD5EFA49BD64931C3CE6CB2CC4482388C056181E40170335BEF992A1BC1E29E83D9EEFAA3D5704F43D5EEB5BC209DC1EC23C40863C06AFDBC33291B73F6F5477BC87133BC0201A103D7B5204BE88C027BE03CE1242FB6687BD058A8A3B1E94C03F841660C35FCC07BF406F97BEC1661CBFAA8CD13C7CAFEABCE257133E1FEA813D441E993D0D49043C68F9633EE03CA2BE5BA2B83DB98BDE41B0BD263D2A512D4391DD3FC5370734BE96E0F23BC873CABDAB97833DE59BB23C9B1EA43F4E749CBD06FCB0BCA17E053FABA1E5BFC10304BF6001BB3E8E66923FB01C87BC46C093BBF7D16140E24B0D3C902C20BDDAE0E73D000336BFF4729EBDB955DFBE0DCC0A417D0D66BC878A1A3EA4CBDABCC213AF3F0ABFCD3E72D8A7403445B0BD218E17BC0B550A3D15EF3EC90DF1ABBF45D938BE91ABE9BCC8A4BD425A821B3E435087C07ABBC53B48DF1F3D0766AD3CFF09F0C6CD8B853C432664BCCA3D513F0E10AE3CE6F93CBB2A8313BF057A933DBD99EA3FC0E1643DC156123F247EB73D0C6BDCBC878281BDC3BBC0BE29BC96BFFDECACBB90EB86BD4A9096BDAECB00BCC97BF8BF7302ECBBC2159842834950BCB23FEBC27CC9A43DCB544E3DC9B1B1C052EC783DD4BE9BBE0D12653BFD360CBD041BE73C9F5C83BD1E6105BB4A87843F5B12B3BD9010FCBEC304393DABAE48BF69C80942323D063F0F219B3E42556CBF1E80B13B2456CFBEE594673BD01626BCD2AB053E3F66A9BDDFA0883ECD6222BCCE5FABBBAE7A243D242FFA40331AF1BED3ADDFBCA775FEC1706D243B6086823DDE8D65C04B513A40D49C87BF47FC7D3E5B58DE3D261DD23EB7A1CC3E68E0133C26FC1DBC530ED13E3D0004C1E5C934BE4D2BFABF733135BF7AAB613B6464843F052DA7BEF331923EB7B589BCA64FE73C4CE0043C905D843CBCEE8A41642E00BD6DE1FB3EC47DF93B9EB5CC3C37DF723D8B22573DCD2472BE2D5085BC30391F3FA0AA713C65B7D9BED34DCBBEF00FE7BD16D4B1C3F7FFA13A894AD83B161906C121836FBB7378E1BBDC182C3C7240093F9858D5BEDD0D1ABECEC1943C9EEC8B40CAFBF4BDD87B853D7CA4D93C5543E43F255F67457CA09D41DFADA1BC09F0DB409C19E3BFF2724FC4FFC2DEBEDE0BBB3B4FE3B33E341963BDE2CF3FBB8C90C1BDD537E7BC2CA616BDFF7DF3BD292792BB3B9B723CB012C2BFAC2D903EF3CC3BBC8064F7BB761186BC325ADBBA0A429AC2934294BC50E008BDD7C690BF67551EBD656C953C7571103FC10341BE443D8C3E36676ABCBF6CC8BD0B980DBD1630F13D1F5D51BDADBC19BF929DF9BD47188CC6129E87C474D0F9C0F9D7F43C82D6493C27B1403EAF47633C66D67CBCFD73303E6CF3F9BF64E2CD3BDA922CBF2B0D31BDDF5210405478AB3E7CE3F83BA0AEBC40C9F0DD3D0C40953E450DF2BC4EC1BC3C8A691041FE076EC470FB36BF996EE0BD203687C37FB5EEBD08F5DCBB4A4E963E3FCF4D3E704BD8BF8EC84CBD6575BE3C09D8DFBCD09CC53D0F4E70BF3F48CF3C15384CC084999ABDF86CF33CE24B563E58B753449EB78AC231808344D51D6CC0EE3062C411D333BF3ACE813F0C9F1A3FFD57EE3D1DA040BEBF3B943FF0F713BE039857BE06496F3BD4399B3CE5CBC1BD63463D3EDB62493CF48FA93DB580FF409E3EBFBC92D800444EF540BDAE7B3A3B139594BB43C9AEC04FC3B9BD66ECB4459C120E3E29B0C1BC622730C016B2993F47E62B47592192BFF5E9A6BD72A85BC1499DA7BB027C7EC353D610441A11E3C0EA3B633C2F4C68C2BBF55FBE59FD043FBFF940C16A6CDFBD11B257C1EDCFC63EDA19F03E60A02C3CCEB3DEC59C76CAC5649DAA3B129F3E3E1905ED3BCC6A9CC2601846C0B1DE333DB4B0BEBC842293C02C6EBE3C55EEE43E2EB827BE6A6D0CBDB4BE4940436C9DBE49594ABC02534CC1ED19CFC5609461BC619C0F3D3EE0D83CDD25263C4F9E5FC302B6843F1D598E3DA66433C0D349F4BD888CD0BF8C5FD94197EF58C0C77BADBE8D1704C304128B40FB116ABE6AC3663EBFFE05BD3FC31B49DB0A97BEDFBB053DA7D2EB3F789C0AC0485A02BD7DF6DFC3BC4C55C0C3C1BF41459917BC6841663B497D7A3FC6211140638296BB01FDC13DB3EBD3BFD8D27FBEF2D8464019418AC2D12619BE39E60FBD706F3C3CD23262BEAC69543C63AD38BD9D2AD33FDC182E3F2DB04CBC07988FC16897FC3C8C68803C3C8048BF653536BD900FB4BD51927BBE920603BCEE0FF7BC5B3BA63CA1D231BC9765BC3BAE50D3BEB99173BD315A3EBF81A5F941DF7F0F3ED8F36CBDD70FFA3C5DFCC7BED6C3CCBD25186A3CC62A383D9DEC41BB67B802BD17BE654223458EBCE2380E47E7DA97BE7B89943C45A708C54C1620C0476E9C439769AABF95C49EC010BA3B41AE75833E532D9EBB1F118BBF11A885BCC856AD3C29C9E643896990C3A0FD083EB560893CEA14203E7DD08A3FB6C8B33C7CCCA24516F0DBC1CE460EBC634482C1B381A1BD8556FCBD97EBC13B2C720848B41117BD9B1EA04086EBF4BD5A02363E"> : tensor<20x30xf32>
    return %0 : tensor<20x30xf32>
  }
}
