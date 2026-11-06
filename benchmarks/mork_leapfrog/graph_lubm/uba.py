#!/usr/bin/env python3
"""A Python port of the LUBM data generator UBA 1.7 (Guo, Pan, Heflin,
"LUBM: A benchmark for OWL knowledge base systems", J. Web Semantics 3(2-3),
2005; http://swat.cse.lehigh.edu/projects/lubm/).

It follows edu.lehigh.swat.bench.uba.Generator statement by statement,
including every random draw (java.util.Random is emulated exactly, and draws
whose results UBA discards are still made), so `-univ N -index 0 -seed 0`
gives the same triples as the original.  The triples are those the OWL
writer emits:

  startSection(C, id)                -> (id rdf:type C)
  addProperty(P, value, resource?)   -> (subject P value)
  addProperty(P, C, id)              -> (subject P id) and (id rdf:type C)

The check against the published reference answers for LUBM(1,0) is in
oracle.py.
"""

MASK48 = (1 << 48) - 1


class JavaRandom:
    """java.util.Random: 48-bit linear congruential generator."""
    MULT, ADD = 0x5DEECE66D, 0xB

    def __init__(self, seed=0):
        self.set_seed(seed)

    def set_seed(self, seed):
        self.seed = (seed ^ self.MULT) & MASK48

    def next(self, bits):
        self.seed = (self.seed * self.MULT + self.ADD) & MASK48
        r = self.seed >> (48 - bits)
        if r >= 1 << 31:
            r -= 1 << 32
        return r

    def next_int(self, bound):
        if bound <= 0:
            raise ValueError('bound must be positive')
        if bound & -bound == bound:
            return (bound * self.next(31)) >> 31
        while True:
            bits = self.next(31)
            val = bits % bound
            if bits - val + (bound - 1) < (1 << 31):   # Java: no int overflow
                return val


# classes (index order matters: it is the order of the random draws)
UNIV, DEPT, FACULTY, PROF, FULLPROF, ASSOPROF, ASSTPROF, LECTURER, STUDENT, UNDERSTUD, \
    GRADSTUD, TA, RA, COURSE, GRADCOURSE, PUBLICATION, CHAIR, RESEARCH, RESEARCHGROUP = range(19)
CLASS_INFO = [(2, -1), (1, -1), (0, -1), (0, FACULTY), (0, PROF), (0, PROF), (0, PROF), (0, FACULTY),
              (0, -1), (0, STUDENT), (0, STUDENT), (0, -1), (0, -1), (0, -1), (0, -1), (0, -1),
              (0, -1), (0, -1), (0, -1)]
CLASS_TOKEN = ["University", "Department", "Faculty", "Professor", "FullProfessor", "AssociateProfessor",
               "AssistantProfessor", "Lecturer", "Student", "UndergraduateStudent", "GraduateStudent",
               "TeachingAssistant", "ResearchAssistant", "Course", "GraduateCourse", "Publication",
               "Chair", "Research", "ResearchGroup"]
CLASS_NUM = len(CLASS_INFO)

UNDER_COURSE_NUM, GRAD_COURSE_NUM, UNIV_NUM, RESEARCH_NUM = 100, 100, 1000, 30
DEPT_MIN, DEPT_MAX = 15, 25
FULLPROF_PUB, ASSOPROF_PUB, ASSTPROF_PUB, GRADSTUD_PUB, LEC_PUB = (15, 20), (10, 18), (5, 10), (0, 5), (0, 5)
FACULTY_COURSE, FACULTY_GRADCOURSE = (1, 2), (1, 2)
UNDERSTUD_COURSE, GRADSTUD_COURSE = (2, 4), (1, 3)
RESEARCHGROUP_RANGE = (10, 20)
FULLPROF_RANGE, ASSOPROF_RANGE, ASSTPROF_RANGE, LEC_RANGE = (7, 10), (10, 14), (8, 11), (5, 7)
R_UNDERSTUD_FACULTY, R_GRADSTUD_FACULTY = (8, 14), (3, 4)
R_GRADSTUD_TA, R_GRADSTUD_RA = (4, 5), (3, 4)
R_UNDERSTUD_ADVISOR, R_GRADSTUD_ADVISOR = 5, 1


class Generator:
    def __init__(self, emit):
        self.emit = emit                     # emit(s, p, o, literal: bool)
        self.num = [0] * CLASS_NUM
        self.total = [0] * CLASS_NUM
        self.count = [0] * CLASS_NUM
        self.random = JavaRandom()
        self.under_courses, self.grad_courses = [], []
        self.remaining_under, self.remaining_grad = [], []
        self.publications = []
        self.chair = 0
        self.subject = None

    # ---- writer (OWL): sections and properties
    def start_section(self, ctype, ident):
        self.subject = ident
        self.emit(ident, 'type', CLASS_TOKEN[ctype], False)

    def add_property(self, prop, value, is_resource):
        self.emit(self.subject, prop, value, not is_resource)

    def add_property_class(self, prop, vclass, vid):
        self.emit(self.subject, prop, vid, False)
        self.emit(vid, 'type', CLASS_TOKEN[vclass], False)

    # ---- generator
    def rand_range(self, lo, hi):
        return lo + self.random.next_int(hi - lo + 1)

    def start(self, univ_num, start_index, seed):
        self.base_seed = seed
        self.num[UNIV] = univ_num
        self.count[UNIV] = start_index
        self.start_index = start_index
        for i in range(univ_num):
            self.generate_univ(i + start_index)

    def set_instance_info(self):
        for i in range(CLASS_NUM):
            if i in (UNIV, DEPT):
                pass
            elif i == FULLPROF:
                self.num[i] = self.rand_range(*FULLPROF_RANGE)
            elif i == ASSOPROF:
                self.num[i] = self.rand_range(*ASSOPROF_RANGE)
            elif i == ASSTPROF:
                self.num[i] = self.rand_range(*ASSTPROF_RANGE)
            elif i == LECTURER:
                self.num[i] = self.rand_range(*LEC_RANGE)
            elif i == UNDERSTUD:
                self.num[i] = self.rand_range(R_UNDERSTUD_FACULTY[0] * self.total[FACULTY],
                                              R_UNDERSTUD_FACULTY[1] * self.total[FACULTY])
            elif i == GRADSTUD:
                self.num[i] = self.rand_range(R_GRADSTUD_FACULTY[0] * self.total[FACULTY],
                                              R_GRADSTUD_FACULTY[1] * self.total[FACULTY])
            elif i == TA:
                self.num[i] = self.rand_range(self.total[GRADSTUD] // R_GRADSTUD_TA[1],
                                              self.total[GRADSTUD] // R_GRADSTUD_TA[0])
            elif i == RA:
                self.num[i] = self.rand_range(self.total[GRADSTUD] // R_GRADSTUD_RA[1],
                                              self.total[GRADSTUD] // R_GRADSTUD_RA[0])
            elif i == RESEARCHGROUP:
                self.num[i] = self.rand_range(*RESEARCHGROUP_RANGE)
            else:
                self.num[i] = CLASS_INFO[i][0]
            self.total[i] = self.num[i]
            sub = i
            while CLASS_INFO[sub][1] != -1:
                sup = CLASS_INFO[sub][1]
                self.total[sup] += self.num[i]
                sub = sup

    def generate_univ(self, index):
        # Java: baseSeed_ * (Integer.MAX_VALUE + 1) + index, the int sum overflowing to -2^31
        seed = self.base_seed * (-(1 << 31)) + index
        self.random.set_seed(seed & ((1 << 64) - 1) if seed < 0 else seed)
        self.num[DEPT] = self.rand_range(DEPT_MIN, DEPT_MAX)
        self.count[DEPT] = 0
        for i in range(self.num[DEPT]):
            self.generate_dept(index, i)

    def generate_dept(self, univ_index, index):
        self.set_instance_info()
        self.under_courses.clear(); self.grad_courses.clear()
        self.remaining_under = list(range(UNDER_COURSE_NUM))
        self.remaining_grad = list(range(GRAD_COURSE_NUM))
        self.publications.clear()
        self.chair = self.random.next_int(self.total[FULLPROF])
        if index == 0:
            self.generate_section(UNIV, univ_index)
        self.generate_section(DEPT, index)
        for i in range(DEPT + 1, CLASS_NUM):
            self.count[i] = 0
            for j in range(self.num[i]):
                self.generate_section(i, j)
        for pub in self.publications:
            self.start_section(PUBLICATION, pub['id'])
            self.add_property('name', pub['name'], False)
            for a in pub['authors']:
                self.add_property('publicationAuthor', a, True)
        for c in self.under_courses:
            self.generate_course(COURSE, c[1])
        for c in self.grad_courses:
            self.generate_course(GRADCOURSE, c[1])
        self.generate_ra_ta()

    def update_count(self, ctype):
        self.count[ctype] += 1
        sub = ctype
        while CLASS_INFO[sub][1] != -1:
            sup = CLASS_INFO[sub][1]
            self.count[sup] += 1
            sub = sup

    def generate_section(self, ctype, index):
        self.update_count(ctype)
        if ctype == UNIV:
            self.start_section(UNIV, self.get_id(UNIV, index))
            self.add_property('name', self.rel_name(UNIV, index), False)
        elif ctype == DEPT:
            self.start_section(DEPT, self.get_id(DEPT, index))
            self.add_property('name', self.rel_name(DEPT, index), False)
            self.add_property_class('subOrganizationOf', UNIV, self.get_id(UNIV, self.count[UNIV] - 1))
        elif ctype == FACULTY:
            self.start_section(FACULTY, self.get_id(FACULTY, index))
            self.faculty_a(FACULTY, index)
        elif ctype == PROF:
            self.start_section(PROF, self.get_id(PROF, index))
            self.prof_a(PROF, index)
        elif ctype == FULLPROF:
            ident = self.get_id(FULLPROF, index)
            self.start_section(FULLPROF, ident)
            self.prof_a(FULLPROF, index)
            if index == self.chair:
                self.add_property('headOf', self.get_id(DEPT, self.count[DEPT] - 1), True)
            self.faculty_publications(ident, *FULLPROF_PUB)
        elif ctype == ASSOPROF:
            ident = self.get_id(ASSOPROF, index)
            self.start_section(ASSOPROF, ident)
            self.prof_a(ASSOPROF, index)
            self.faculty_publications(ident, *ASSOPROF_PUB)
        elif ctype == ASSTPROF:
            ident = self.get_id(ASSTPROF, index)
            self.start_section(ASSTPROF, ident)
            self.prof_a(ASSTPROF, index)
            self.faculty_publications(ident, *ASSTPROF_PUB)
        elif ctype == LECTURER:
            ident = self.get_id(LECTURER, index)
            self.start_section(LECTURER, ident)
            self.faculty_a(LECTURER, index)
            self.faculty_publications(ident, *LEC_PUB)
        elif ctype == UNDERSTUD:
            self.start_section(UNDERSTUD, self.get_id(UNDERSTUD, index))
            self.student_a(UNDERSTUD, index)
            n = self.rand_range(*UNDERSTUD_COURSE)
            for k in self.random_list(n, 0, len(self.under_courses) - 1):
                self.add_property('takesCourse', self.get_id(COURSE, self.under_courses[k][1]), True)
            if self.random.next_int(R_UNDERSTUD_ADVISOR) == 0:
                self.add_property('advisor', self.select_advisor(), True)
        elif ctype == GRADSTUD:
            ident = self.get_id(GRADSTUD, index)
            self.start_section(GRADSTUD, ident)
            self.student_a(GRADSTUD, index)
            n = self.rand_range(*GRADSTUD_COURSE)
            for k in self.random_list(n, 0, len(self.grad_courses) - 1):
                self.add_property('takesCourse', self.get_id(GRADCOURSE, self.grad_courses[k][1]), True)
            self.add_property_class('undergraduateDegreeFrom', UNIV,
                                    self.get_id(UNIV, self.random.next_int(UNIV_NUM)))
            if self.random.next_int(R_GRADSTUD_ADVISOR) == 0:
                self.add_property('advisor', self.select_advisor(), True)
            num = self.rand_range(*GRADSTUD_PUB)
            for k in self.random_list(num, 0, len(self.publications) - 1):
                self.publications[k]['authors'].append(ident)
        elif ctype == COURSE:
            self.generate_course(COURSE, index)
        elif ctype == GRADCOURSE:
            self.generate_course(GRADCOURSE, index)
        elif ctype == RESEARCHGROUP:
            self.start_section(RESEARCHGROUP, self.get_id(RESEARCHGROUP, index))
            self.add_property('subOrganizationOf', self.get_id(DEPT, self.count[DEPT] - 1), True)
        # TA, RA and the remaining classes have no section here

    def generate_course(self, ctype, index):
        self.start_section(ctype, self.get_id(ctype, index))
        self.add_property('name', self.rel_name(ctype, index), False)

    def faculty_a(self, ctype, index):
        index_in_faculty = self.count[FACULTY] - 1
        self.add_property('name', self.rel_name(ctype, index), False)
        for _ in range(self.rand_range(*FACULTY_COURSE)):
            self.rand_range(0, len(self.remaining_under) - 1)       # drawn and discarded, as in UBA
            g = self.remaining_under.pop(0)
            self.under_courses.append((index_in_faculty, g))
            self.add_property('teacherOf', self.get_id(COURSE, g), True)
        for _ in range(self.rand_range(*FACULTY_GRADCOURSE)):
            self.rand_range(0, len(self.remaining_grad) - 1)
            g = self.remaining_grad.pop(0)
            self.grad_courses.append((index_in_faculty, g))
            self.add_property('teacherOf', self.get_id(GRADCOURSE, g), True)
        self.add_property_class('undergraduateDegreeFrom', UNIV, self.get_id(UNIV, self.random.next_int(UNIV_NUM)))
        self.add_property_class('mastersDegreeFrom', UNIV, self.get_id(UNIV, self.random.next_int(UNIV_NUM)))
        self.add_property_class('doctoralDegreeFrom', UNIV, self.get_id(UNIV, self.random.next_int(UNIV_NUM)))
        self.add_property('worksFor', self.get_id(DEPT, self.count[DEPT] - 1), True)
        self.add_property('emailAddress', self.email(ctype, index), False)
        self.add_property('telephone', 'xxx-xxx-xxxx', False)

    def prof_a(self, ctype, index):
        self.faculty_a(ctype, index)
        self.add_property('researchInterest', self.rel_name(RESEARCH, self.random.next_int(RESEARCH_NUM)), False)

    def faculty_publications(self, author, lo, hi):
        for i in range(self.rand_range(lo, hi)):
            self.publications.append({'id': f"{author}/{CLASS_TOKEN[PUBLICATION]}{i}",
                                      'name': self.rel_name(PUBLICATION, i), 'authors': [author]})

    def student_a(self, ctype, index):
        self.add_property('name', self.rel_name(ctype, index), False)
        self.add_property('memberOf', self.get_id(DEPT, self.count[DEPT] - 1), True)
        self.add_property('emailAddress', self.email(ctype, index), False)
        self.add_property('telephone', 'xxx-xxx-xxxx', False)

    def select_advisor(self):
        ptype = self.rand_range(FULLPROF, ASSTPROF)
        return self.get_id(ptype, self.random.next_int(self.total[ptype]))

    def generate_ra_ta(self):
        lst = self.random_list(self.total[TA] + self.total[RA], 0, self.total[GRADSTUD] - 1)
        courses = self.random_list(self.total[TA], 0, len(self.under_courses) - 1)
        i = 0
        while i < self.total[TA]:
            self.start_section(TA, self.get_id(GRADSTUD, lst[i]))
            self.add_property('teachingAssistantOf', self.get_id(COURSE, self.under_courses[courses[i]][1]), True)
            i += 1
        while i < len(lst):
            self.start_section(RA, self.get_id(GRADSTUD, lst[i]))
            i += 1

    def random_list(self, num, lo, hi):
        tmp = list(range(lo, hi + 1))
        out = []
        for _ in range(num):
            pos = self.rand_range(0, len(tmp) - 1)
            out.append(tmp.pop(pos))
        return out

    # ---- names
    def rel_name(self, ctype, index):
        return f"{CLASS_TOKEN[ctype]}{index}"

    def get_id(self, ctype, index):
        if ctype == UNIV:
            return f"http://www.{self.rel_name(UNIV, index)}.edu"
        if ctype == DEPT:
            return f"http://www.{self.rel_name(DEPT, index)}.{self.rel_name(UNIV, self.count[UNIV] - 1)}.edu"
        return f"{self.get_id(DEPT, self.count[DEPT] - 1)}/{self.rel_name(ctype, index)}"

    def email(self, ctype, index):
        return (f"{self.rel_name(ctype, index)}@{self.rel_name(DEPT, self.count[DEPT] - 1)}."
                f"{self.rel_name(UNIV, self.count[UNIV] - 1)}.edu")


def generate(univ_num, start_index=0, seed=0):
    """All triples (s, p, o, is_literal) of LUBM(univ_num, seed) in UBA's order."""
    triples = []
    g = Generator(lambda s, p, o, lit: triples.append((s, p, o, lit)))
    g.start(univ_num, start_index, seed)
    return triples


if __name__ == '__main__':
    import sys
    t = generate(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
    print(len(t), 'triples')
