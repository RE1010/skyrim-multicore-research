"""Extract xperf butterfly HTML tables and verify exclusive sample totals."""
import argparse
import json
from html.parser import HTMLParser
from pathlib import Path

class Tables(HTMLParser):
    def __init__(self):
        super().__init__()
        self.heading = self.table = self.row = self.cell = None
        self.rows = {}
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'a' and attrs.get('id', '').startswith('Tbl'):
            self.heading = attrs['id']
        if tag == 'table':
            self.table = self.heading
            self.rows.setdefault(self.table, [])
        if tag == 'tr': self.row = []
        if tag in ('td', 'th'): self.cell = []
    def handle_data(self, data):
        if self.cell is not None: self.cell.append(data)
    def handle_endtag(self, tag):
        if tag in ('td', 'th') and self.cell is not None:
            if self.row is not None: self.row.append(''.join(self.cell).strip())
            self.cell = None
        if tag == 'tr' and self.row is not None:
            if self.table is not None: self.rows[self.table].append(self.row)
            self.row = None
        if tag == 'table': self.table = None

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    args = parser.parse_args()
    parsed = Tables()
    parsed.feed(args.input.read_text(encoding='utf-8'))
    total = sum(int(row[2]) for row in parsed.rows['TblP'][1:] if row[1])
    module_total = sum(int(row[1]) for row in parsed.rows['TblME'][1:])
    exclusive_total = sum(int(row[1]) for row in parsed.rows['TblSE'][1:])
    if not total or total != module_total or total != exclusive_total:
        raise ValueError(f'Inconsistent exclusive totals: roots={total}, modules={module_total}, functions={exclusive_total}')
    output = args.input.with_suffix('.json')
    output.write_text(json.dumps(parsed.rows, indent=2), encoding='utf-8')
    print(json.dumps({'output': str(output), 'verifiedSamples': total}))
