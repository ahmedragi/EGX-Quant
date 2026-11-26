param([string]$OutDir)
# Usage: powershell -File tests/fixtures/tbills/make_fixtures.ps1 -OutDir tests/fixtures/tbills
# Builds small hand-written .xlsx fixtures for the T-bill importer tests.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function New-Xlsx([string]$path, [hashtable]$parts) {
    if (Test-Path $path) { Remove-Item $path }
    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::CreateNew)
    $zip = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Create)
    foreach ($name in ($parts.Keys | Sort-Object)) {
        $entry = $zip.CreateEntry($name)
        $w = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
        $w.Write($parts[$name]); $w.Dispose()
    }
    $zip.Dispose(); $fs.Dispose()
}

$ns = 'xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'
$common = @{
    '[Content_Types].xml' = '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet3.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/></Types>'
    '_rels/.rels' = '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'
    'xl/workbook.xml' = "<?xml version=`"1.0`" encoding=`"UTF-8`"?><workbook $ns><sheets><sheet name=`"91 days`" sheetId=`"1`" r:id=`"rId1`"/><sheet name=`"182 days`" sheetId=`"2`" r:id=`"rId3`"/><sheet name=`"Notes`" sheetId=`"3`" r:id=`"rId4`"/></sheets></workbook>"
    'xl/_rels/workbook.xml.rels' = '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="/xl/worksheets/sheet2.xml"/><Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet3.xml"/></Relationships>'
    'xl/sharedStrings.xml' = "<?xml version=`"1.0`" encoding=`"UTF-8`"?><sst $ns count=`"9`" uniqueCount=`"9`"><si><t>EGP T-Bills</t></si><si><t>Tenor (days)</t></si><si><t>ISIN</t></si><si><t>Issue Date</t></si><si><t>Maturity Date</t></si><si><r><t>Weighted Avg. </t></r><r><rPr><b/></rPr><t>Yield (%)</t></r></si><si><t>EGT0001</t></si><si><t>Notes &amp; sources</t></si><si><t>EGT0002</t></si></sst>"
    'xl/worksheets/sheet2.xml' = "<?xml version=`"1.0`" encoding=`"UTF-8`"?><worksheet $ns><cols><col min=`"1`" max=`"5`" width=`"16`"/></cols><sheetData><row r=`"1`"><c r=`"A1`" t=`"s`"><v>1</v></c><c r=`"C1`" t=`"s`"><v>3</v></c><c r=`"D1`" t=`"s`"><v>4</v></c><c r=`"E1`" t=`"s`"><v>5</v></c></row><row r=`"2`"><c r=`"A2`"><v>182</v></c><c r=`"C2`"><v>46350</v></c><c r=`"D2`"><v>46532</v></c><c r=`"E2`"><v>25.358</v></c></row></sheetData></worksheet>"
    'xl/worksheets/sheet3.xml' = "<?xml version=`"1.0`" encoding=`"UTF-8`"?><worksheet $ns><sheetData><row r=`"1`"><c r=`"A1`" t=`"s`"><v>7</v></c></row></sheetData></worksheet>"
}

function Sheet91([string]$maturity1) {
    "<?xml version=`"1.0`" encoding=`"UTF-8`"?><worksheet $ns><sheetData>" +
    "<row r=`"1`"><c r=`"A1`" t=`"s`"><v>0</v></c><c r=`"B1`"/></row>" +
    "<row r=`"2`"><c r=`"A2`" t=`"s`"><v>1</v></c><c r=`"B2`" t=`"s`"><v>2</v></c><c r=`"C2`" t=`"s`"><v>3</v></c><c r=`"D2`" t=`"s`"><v>4</v></c><c r=`"E2`" t=`"s`"><v>5</v></c></row>" +
    "<row r=`"3`"><c r=`"A3`"><v>91</v></c><c r=`"B3`" t=`"s`"><v>6</v></c><c r=`"C3`" s=`"6`"><v>46350</v></c><c r=`"D3`" s=`"6`"><v>$maturity1</v></c><c r=`"E3`"><v>24.917</v></c></row>" +
    "<row r=`"4`"><c r=`"A4`"><v>91</v></c><c r=`"B4`" t=`"inlineStr`"><is><t>EGT0003</t></is></c><c r=`"C4`" t=`"inlineStr`"><is><t>17/11/2026</t></is></c><c r=`"D4`" t=`"inlineStr`"><is><t>16/02/2027</t></is></c><c r=`"E4`"><v>24.608</v></c></row>" +
    "<row r=`"5`"><c r=`"A5`"><v>91</v></c><c r=`"B5`" t=`"s`"><v>8</v></c><c r=`"C5`"><v>46336</v></c><c r=`"D5`"><v>46427</v></c><c r=`"E5`"/></row>" +
    "</sheetData></worksheet>"
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$good = $common.Clone(); $good['xl/worksheets/sheet1.xml'] = Sheet91 '46441'
New-Xlsx (Join-Path $OutDir 'auctions_small.xlsx') $good
$bad = $common.Clone(); $bad['xl/worksheets/sheet1.xml'] = Sheet91 '46532'
New-Xlsx (Join-Path $OutDir 'auctions_bad_maturity.xlsx') $bad
Get-ChildItem $OutDir | Select-Object Name, Length
